//===----------------------------------------------------------------------===//
///
/// \file
/// Captura y reproducción de tramas RF con CC1101.
///
//===----------------------------------------------------------------------===//

#include "modules/rf.h"
#include "drivers/cc1101.h"

/// Timeout máximo para esperar una recepción completa (ms).
#define RF_CAPTURE_TIMEOUT_MS 5000
/// Timeout máximo para esperar fin de transmisión (ms).
#define RF_TX_TIMEOUT_MS 2000
/// Gap que se toma como fin de trama (us).
#define RF_END_GAP_US 12000

static uint16_t rfPulseUs[RF_CAPTURE_PULSES_MAX];
static uint16_t rfPulseCount = 0;
static bool_t rfFirstLevel = FALSE;
static bool_t rfCaptureValid = FALSE;

/// Convierte ciclos de clock a microsegundos.
/// \param cycles cantidad de ciclos
/// \return tiempo en microsegundos
static uint32_t rfCyclesToUs(uint32_t cycles) {
  return (uint32_t)(((uint64_t)cycles * 1000000ULL) /
                    (uint64_t)SystemCoreClock);
}

/// Aplica perfil 433 MHz OOK async serial para captura genérica.
static bool_t rfApply433Profile(void) {
  cc1101OokConfig_t cfg = CC1101_OOK_CONFIG_433;
  cfg.addressCheckEnable = FALSE;
  cfg.crcEnable = FALSE;
  cfg.variableLength = FALSE;
  cfg.packetLength = 0xFF;
  cfg.rxBandwidthHz = 101000;
  cfg.asyncSerialMode = TRUE;
  cfg.dataRateBps = 5000;

  bool_t ok = cc1101_applyOokConfig(&cfg);
#ifdef MEDIUM_DEBUG
  if (ok) {
    printf("[modules/rf] Perfil 433MHz aplicado (BW=101kHz)\r\n");
  } else {
    printf("[modules/rf] ERROR: no se pudo aplicar perfil 433MHz\r\n");
  }
#endif
  return ok;
}

/// Inicializa RF para capturar/reproducir a 433 MHz.
bool_t rfInit(void) {
  if (!cc1101_init()) {
#ifdef MEDIUM_DEBUG
    printf("[modules/rf] ERROR: cc1101_init() fallo\r\n");
#endif
    return FALSE;
  }

  if (!rfApply433Profile()) {
    return FALSE;
  }

  cc1101_enterRx();
#ifdef MEDIUM_DEBUG
  printf("[modules/rf] RF listo en RX (433MHz)\r\n");
#endif
  return TRUE;
}

/// Captura una trama recibida y la guarda como secuencia de pulsos.
bool_t rfCapture315MHz(void) {
  bool_t seenPacket = FALSE;
  uint32_t elapsed = 0;
  bool_t level;
  bool_t lastLevel;
  uint32_t lastEdgeCycles;
  uint32_t nowCycles;

  if (!rfApply433Profile()) {
    return FALSE;
  }

  cc1101_enterRx();

#ifdef MEDIUM_DEBUG
  printf("[modules/rf] Capturando señal en 433MHz...\r\n");
#endif

  level = cc1101_gdo0Read();
  while (elapsed < RF_CAPTURE_TIMEOUT_MS) {
    bool_t cur = cc1101_gdo0Read();
    if (cur != level) {
      break;
    }
    delay(1);
    elapsed++;
  }

  if (elapsed >= RF_CAPTURE_TIMEOUT_MS) {
#ifdef MEDIUM_DEBUG
    printf("[modules/rf] ERROR: timeout esperando inicio de señal\r\n");
#endif
    return FALSE;
  }

  rfPulseCount = 0;
  rfCaptureValid = FALSE;
  rfFirstLevel = !level;
  lastLevel = rfFirstLevel;
  lastEdgeCycles = cyclesCounterRead();

  while (rfPulseCount < RF_CAPTURE_PULSES_MAX) {
    nowCycles = cyclesCounterRead();
    level = cc1101_gdo0Read();

    if (level != lastLevel) {
      uint32_t dtUs = rfCyclesToUs(nowCycles - lastEdgeCycles);
      if (dtUs > 0xFFFFUL) {
        dtUs = 0xFFFFUL;
      }
      rfPulseUs[rfPulseCount++] = (uint16_t)dtUs;
      lastEdgeCycles = nowCycles;
      lastLevel = level;
    } else {
      uint32_t idleUs = rfCyclesToUs(nowCycles - lastEdgeCycles);
      if (idleUs > RF_END_GAP_US && rfPulseCount > 8) {
        break;
      }
    }
  }

  if (rfPulseCount == 0) {
#ifdef MEDIUM_DEBUG
    printf("[modules/rf] ERROR: no se capturaron pulsos\r\n");
#endif
    return FALSE;
  }

  rfCaptureValid = TRUE;

#ifdef MEDIUM_DEBUG
  printf("[modules/rf] Captura OK: %u pulsos, nivel inicial=%u\r\n",
         rfPulseCount, rfFirstLevel);
  printf("[modules/rf] Primeros pulsos(us): ");
  for (uint16_t i = 0; i < rfPulseCount && i < 10; i++) {
    printf("%u ", rfPulseUs[i]);
  }
  printf("\r\n");
#endif

  return TRUE;
}

/// Reproduce la última señal capturada como pulsos OOK.
bool_t rfReplayCaptured(void) {
  bool_t level;
  bool_t txStarted = FALSE;
  uint32_t elapsed = 0;

  if (!rfCaptureValid || rfPulseCount == 0) {
#ifdef MEDIUM_DEBUG
    printf("[modules/rf] ERROR: no hay captura valida para reproducir\r\n");
#endif
    return FALSE;
  }

  if (!rfApply433Profile()) {
    return FALSE;
  }

  gpioInit(CC1101_GDO0_PIN, GPIO_OUTPUT);
  level = rfFirstLevel;
  gpioWrite(CC1101_GDO0_PIN, level);

  if (!cc1101_enterTx()) {
#ifdef MEDIUM_DEBUG
    printf("[modules/rf] ERROR: no se pudo entrar en TX\r\n");
#endif
    gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
    return FALSE;
  }

  for (uint16_t i = 0; i < rfPulseCount; i++) {
    delayInaccurateUs(rfPulseUs[i]);
    level = !level;
    gpioWrite(CC1101_GDO0_PIN, level);
  }

  while (elapsed < RF_TX_TIMEOUT_MS) {
    bool_t gdo0 = cc1101_gdo0Read();
    if (gdo0) {
      txStarted = TRUE;
    } else if (txStarted) {
      break;
    }
    delay(1);
    elapsed++;
  }

  cc1101_enterIdle();
  gpioWrite(CC1101_GDO0_PIN, FALSE);
  gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
  cc1101_enterRx();

#ifdef MEDIUM_DEBUG
  printf("[modules/rf] Replay OK: %u pulsos enviados\r\n", rfPulseCount);
#endif

  return TRUE;
}

/// Informa si hay una captura RF válida en memoria.
bool_t rfHasCapture(void) { return rfCaptureValid; }
