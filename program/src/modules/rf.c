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
static cc1101Band_t rfCapturedBand = CC1101_BAND_433MHZ;
static cc1101OokConfig_t rfCaptureConfig;
static cc1101OokConfig_t rfCapturedConfig;

/// Convierte ciclos de clock a microsegundos.
/// \param cycles cantidad de ciclos
/// \return tiempo en microsegundos
static uint32_t rfCyclesToUs(uint32_t cycles) {
  return (uint32_t)(((uint64_t)cycles * 1000000ULL) /
                    (uint64_t)SystemCoreClock);
}

/// Aplica la configuración de captura actual.
static bool_t rfApplyCurrentProfile(void) {
  bool_t ok = cc1101_applyOokConfig(&rfCaptureConfig);
#ifdef MEDIUM_DEBUG
  if (ok) {
    printf("[modules/rf] Perfil %s aplicado (BW=%luHz)\r\n",
           (rfCaptureConfig.band == CC1101_BAND_315MHZ) ? "315MHz" : "433MHz",
           (unsigned long)rfCaptureConfig.rxBandwidthHz);
  } else {
    printf("[modules/rf] ERROR: no se pudo aplicar perfil %s\r\n",
           (rfCaptureConfig.band == CC1101_BAND_315MHZ) ? "315MHz" : "433MHz");
  }
#endif
  return ok;
}

/// Carga parámetros de captura por banda en la config genérica actual.
/// \param band banda a configurar
static void rfFillConfigForBand(cc1101Band_t band) {
  rfCaptureConfig = (band == CC1101_BAND_315MHZ) ? CC1101_OOK_CONFIG_315
                                                 : CC1101_OOK_CONFIG_433;
  rfCaptureConfig.addressCheckEnable = FALSE;
  rfCaptureConfig.crcEnable = FALSE;
  rfCaptureConfig.variableLength = FALSE;
  rfCaptureConfig.packetLength = 0xFF;
  rfCaptureConfig.rxBandwidthHz =
      (band == CC1101_BAND_315MHZ) ? 812500 : 101000;
  rfCaptureConfig.asyncSerialMode = TRUE;
  rfCaptureConfig.dataRateBps = 5000;
}

/// Inicializa RF para capturar/reproducir a 433 MHz.
bool_t rfInit(void) {
  if (!cc1101_init()) {
#ifdef MEDIUM_DEBUG
    printf("[modules/rf] ERROR: cc1101_init() fallo\r\n");
#endif
    return FALSE;
  }

  rfFillConfigForBand(CC1101_BAND_433MHZ);
  if (!rfApplyCurrentProfile()) {
    return FALSE;
  }

  cc1101_enterRx();
#ifdef MEDIUM_DEBUG
  printf("[modules/rf] RF listo en RX (433MHz)\r\n");
#endif
  return TRUE;
}

/// Establece una configuración de captura personalizada.
/// \param config configuración OOK completa a usar en capture/replay
bool_t rfSetCaptureConfig(const cc1101OokConfig_t *config) {
  if (config == NULL) {
    return FALSE;
  }
  rfCaptureConfig = *config;
  return rfApplyCurrentProfile();
}

/// Selecciona la banda de captura con parámetros recomendados.
/// \param band banda deseada
bool_t rfSetCaptureBand(cc1101Band_t band) {
  rfFillConfigForBand(band);
  return rfApplyCurrentProfile();
}

/// Captura una trama recibida y la guarda como secuencia de pulsos.
bool_t rfCapture(void) {
  bool_t seenPacket = FALSE;
  uint32_t elapsed = 0;
  bool_t level;
  bool_t lastLevel;
  uint32_t lastEdgeCycles;
  uint32_t nowCycles;

  if (!rfApplyCurrentProfile()) {
    return FALSE;
  }

  cc1101_enterRx();

#ifdef MEDIUM_DEBUG
  printf("[modules/rf] Capturando señal en %s...\r\n",
         (rfCaptureConfig.band == CC1101_BAND_315MHZ) ? "315MHz" : "433MHz");
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
  rfCapturedBand = rfCaptureConfig.band;
  rfCapturedConfig = rfCaptureConfig;

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

/// Captura en banda de 433 MHz.
bool_t rfCapture433MHz(void) {
  if (!rfSetCaptureBand(CC1101_BAND_433MHZ)) {
    return FALSE;
  }
  return rfCapture();
}

/// Captura en banda de 315 MHz.
bool_t rfCapture315MHz(void) {
  if (!rfSetCaptureBand(CC1101_BAND_315MHZ)) {
    return FALSE;
  }
  return rfCapture();
}

/// Reproduce la última señal capturada como pulsos OOK.
bool_t rfReplayCaptured(void) {
  bool_t level;

  if (!rfCaptureValid || rfPulseCount == 0) {
#ifdef MEDIUM_DEBUG
    printf("[modules/rf] ERROR: no hay captura valida para reproducir\r\n");
#endif
    return FALSE;
  } else {
#ifdef MEDIUM_DEBUG
    printf("[modules/rf] Inciando reproducción...\r\n");
#endif
  }

  rfCaptureConfig = rfCapturedConfig;
  if (!rfApplyCurrentProfile()) {
    return FALSE;
  }

  // Evita contención en GDO0: primero pasar el CC1101 a IDLE, luego manejar la
  // línea desde MCU para modo async TX.
  cc1101_enterIdle();
  gpioInit(CC1101_GDO0_PIN, GPIO_OUTPUT);
  level = rfFirstLevel;
  gpioWrite(CC1101_GDO0_PIN, level);

#ifdef MEDIUM_DEBUG
  printf("[modules/rf] Replay cfg: band=%s bw=%lu rate=%lu async=%u iocfg0=0x%02X pktctrl0=0x%02X\r\n",
         (rfCaptureConfig.band == CC1101_BAND_315MHZ) ? "315MHz" : "433MHz",
         (unsigned long)rfCaptureConfig.rxBandwidthHz,
         (unsigned long)rfCaptureConfig.dataRateBps, rfCaptureConfig.asyncSerialMode,
         cc1101_readRegister(CC1101_IOCFG0), cc1101_readRegister(CC1101_PKTCTRL0));
#endif

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
  delay(2);

  cc1101_enterIdle();
  gpioWrite(CC1101_GDO0_PIN, FALSE);
  gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
  cc1101_enterRx();

#ifdef MEDIUM_DEBUG
  printf("[modules/rf] Replay OK: %u pulsos enviados (%s)\r\n", rfPulseCount,
         (rfCapturedBand == CC1101_BAND_315MHZ) ? "315MHz" : "433MHz");
#endif

  return TRUE;
}

/// Informa si hay una captura RF válida en memoria.
bool_t rfHasCapture(void) { return rfCaptureValid; }

/// Devuelve la banda de la última captura.
cc1101Band_t rfLastCaptureBand(void) { return rfCapturedBand; }
