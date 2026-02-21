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
/// Duración máxima permitida por edge al reproducir desde .sig.
#define RF_REPLAY_MAX_EDGE_US 500000UL
/// Duración total máxima permitida al reproducir desde .sig.
#define RF_REPLAY_MAX_TOTAL_US 15000000UL

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

/// Calcula 10^exp para exp>=0 con saturación.
static uint32_t rfPow10U32(uint8_t exp) {
  uint32_t value = 1U;
  for (uint8_t i = 0; i < exp; i++) {
    if (value > (0xFFFFFFFFUL / 10UL)) {
      return 0xFFFFFFFFUL;
    }
    value *= 10U;
  }
  return value;
}

/// Convierte ticks de .sig a microsegundos según tickScale.
static uint32_t rfTicksToUs(uint32_t ticks, int8_t tickScale) {
  if (tickScale >= -6) {
    uint8_t exp = (uint8_t)(tickScale + 6);
    uint32_t mul = rfPow10U32(exp);
    if (mul == 0xFFFFFFFFUL) {
      return 0xFFFFFFFFUL;
    }
    if (ticks > (0xFFFFFFFFUL / mul)) {
      return 0xFFFFFFFFUL;
    }
    return ticks * mul;
  } else {
    uint8_t exp = (uint8_t)(-6 - tickScale);
    uint32_t div = rfPow10U32(exp);
    if (div == 0U) {
      return 1U;
    }
    ticks = (ticks + (div / 2U)) / div;
    return (ticks == 0U) ? 1U : ticks;
  }
}

/// Aplica la configuración de captura actual.
static bool_t rfApplyCurrentProfile(void) {
  bool_t ok = cc1101_applyOokConfig(&rfCaptureConfig);
#ifdef MEDIUM_DEBUG
  if (ok) {
    printf("[modules] [rf] Perfil %s aplicado (BW=%luHz)\r\n",
           (rfCaptureConfig.band == CC1101_BAND_315MHZ) ? "315MHz" : "433MHz",
           (unsigned long)rfCaptureConfig.rxBandwidthHz);
  } else {
    printf("[modules] [rf] ERROR: no se pudo aplicar perfil %s\r\n",
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
    printf("[modules] [rf] ERROR: cc1101_init() fallo\r\n");
#endif
    return FALSE;
  }

  rfFillConfigForBand(CC1101_BAND_433MHZ);
  if (!rfApplyCurrentProfile()) {
    return FALSE;
  }

  cc1101_enterRx();
#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] RF listo en RX (433MHz)\r\n");
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
bool_t rfCaptureWithCancel(rfCancelCallback_t cancelCallback, void *context) {
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
  printf("[modules] [rf] Capturando señal en %s...\r\n",
         (rfCaptureConfig.band == CC1101_BAND_315MHZ) ? "315MHz" : "433MHz");
#endif

  level = cc1101_gdo0Read();
  while (elapsed < RF_CAPTURE_TIMEOUT_MS) {
    bool_t cur = cc1101_gdo0Read();
    if (cancelCallback != NULL && cancelCallback(context)) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [rf] Captura cancelada por callback\r\n");
#endif
      return FALSE;
    }
    if (cur != level) {
      break;
    }
    delay(1);
    elapsed++;
  }

  if (elapsed >= RF_CAPTURE_TIMEOUT_MS) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [rf] ERROR: timeout esperando inicio de señal\r\n");
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

    if (cancelCallback != NULL && cancelCallback(context)) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [rf] Captura cancelada por callback\r\n");
#endif
      return FALSE;
    }

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
    printf("[modules] [rf] ERROR: no se capturaron pulsos\r\n");
#endif
    return FALSE;
  }

  rfCaptureValid = TRUE;
  rfCapturedBand = rfCaptureConfig.band;
  rfCapturedConfig = rfCaptureConfig;

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] Captura OK: %u pulsos, nivel inicial=%u\r\n",
         rfPulseCount, rfFirstLevel);
  printf("[modules] [rf] Primeros pulsos(us): ");
  for (uint16_t i = 0; i < rfPulseCount && i < 10; i++) {
    printf("%u ", rfPulseUs[i]);
  }
  printf("\r\n");
#endif

  return TRUE;
}

/// Captura una trama RF.
bool_t rfCapture(void) { return rfCaptureWithCancel(NULL, NULL); }

/// Captura en banda de 433 MHz.
bool_t rfCapture433MHz(void) {
  if (!rfSetCaptureBand(CC1101_BAND_433MHZ)) {
    return FALSE;
  }
  return rfCapture();
}

/// Captura en banda de 433 MHz con callback de cancelación.
bool_t rfCapture433MHzWithCancel(rfCancelCallback_t cancelCallback,
                                 void *context) {
  if (!rfSetCaptureBand(CC1101_BAND_433MHZ)) {
    return FALSE;
  }
  return rfCaptureWithCancel(cancelCallback, context);
}

/// Captura en banda de 315 MHz.
bool_t rfCapture315MHz(void) {
  if (!rfSetCaptureBand(CC1101_BAND_315MHZ)) {
    return FALSE;
  }
  return rfCapture();
}

/// Captura en banda de 315 MHz con callback de cancelación.
bool_t rfCapture315MHzWithCancel(rfCancelCallback_t cancelCallback,
                                 void *context) {
  if (!rfSetCaptureBand(CC1101_BAND_315MHZ)) {
    return FALSE;
  }
  return rfCaptureWithCancel(cancelCallback, context);
}

/// Reproduce la última señal capturada como pulsos OOK.
bool_t rfReplayCaptured(void) {
  bool_t level;

  if (!rfCaptureValid || rfPulseCount == 0) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [rf] ERROR: no hay captura valida para reproducir\r\n");
#endif
    return FALSE;
  } else {
#ifdef MEDIUM_DEBUG
    printf("[modules] [rf] Inciando reproducción...\r\n");
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
  printf("[modules] [rf] Replay cfg: band=%s bw=%lu rate=%lu async=%u "
         "iocfg0=0x%02X pktctrl0=0x%02X\r\n",
         (rfCaptureConfig.band == CC1101_BAND_315MHZ) ? "315MHz" : "433MHz",
         (unsigned long)rfCaptureConfig.rxBandwidthHz,
         (unsigned long)rfCaptureConfig.dataRateBps,
         rfCaptureConfig.asyncSerialMode, cc1101_readRegister(CC1101_IOCFG0),
         cc1101_readRegister(CC1101_PKTCTRL0));
#endif

  if (!cc1101_enterTx()) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [rf] ERROR: no se pudo entrar en TX\r\n");
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
  printf("[modules] [rf] Replay OK: %u pulsos enviados (%s)\r\n", rfPulseCount,
         (rfCapturedBand == CC1101_BAND_315MHZ) ? "315MHz" : "433MHz");
#endif

  return TRUE;
}

/// Reproduce una señal RF desde flancos cargados de un .sig.
bool_t rfReplayEdges(const uint32_t *edges, uint32_t edgeCount,
                     uint8_t startLevel, int8_t tickScale) {
  bool_t level = startLevel ? TRUE : FALSE;
  uint32_t totalUs = 0U;

  if (edges == NULL || edgeCount == 0U) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [rf] ERROR: rfReplayEdges sin datos\r\n");
#endif
    return FALSE;
  }

  if (!rfApplyCurrentProfile()) {
    return FALSE;
  }

  cc1101_enterIdle();
  gpioInit(CC1101_GDO0_PIN, GPIO_OUTPUT);
  gpioWrite(CC1101_GDO0_PIN, level);

  if (!cc1101_enterTx()) {
    gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
#ifdef MEDIUM_DEBUG
    printf("[modules] [rf] ERROR: rfReplayEdges no pudo entrar en TX\r\n");
#endif
    return FALSE;
  }

  for (uint32_t i = 0; i < edgeCount; i++) {
    uint32_t durationUs = rfTicksToUs(edges[i], tickScale);
    if (durationUs == 0U) {
      durationUs = 1U;
    }
    if (durationUs > RF_REPLAY_MAX_EDGE_US) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [rf] ERROR: edge demasiado largo (%luus)\r\n",
             (unsigned long)durationUs);
#endif
      cc1101_enterIdle();
      gpioWrite(CC1101_GDO0_PIN, FALSE);
      gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
      cc1101_enterRx();
      return FALSE;
    }
    if (totalUs > (RF_REPLAY_MAX_TOTAL_US - durationUs)) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [rf] ERROR: replay excede %luus\r\n",
             (unsigned long)RF_REPLAY_MAX_TOTAL_US);
#endif
      cc1101_enterIdle();
      gpioWrite(CC1101_GDO0_PIN, FALSE);
      gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
      cc1101_enterRx();
      return FALSE;
    }
    totalUs += durationUs;
    delayInaccurateUs(durationUs);
    level = !level;
    gpioWrite(CC1101_GDO0_PIN, level);
  }
  delay(2);

  cc1101_enterIdle();
  gpioWrite(CC1101_GDO0_PIN, FALSE);
  gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
  cc1101_enterRx();

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] Replay desde .sig OK (%lu edges, scale=%d)\r\n",
         (unsigned long)edgeCount, (int)tickScale);
#endif
  return TRUE;
}

/// Informa si hay una captura RF válida en memoria.
bool_t rfHasCapture(void) { return rfCaptureValid; }

/// Devuelve la última captura RF.
bool_t rfGetLastCapture(const uint16_t **pulsesUs, uint16_t *count,
                        bool_t *firstLevel) {
  if (!rfCaptureValid || pulsesUs == NULL || count == NULL ||
      firstLevel == NULL) {
    return FALSE;
  }

  *pulsesUs = rfPulseUs;
  *count = rfPulseCount;
  *firstLevel = rfFirstLevel;
  return TRUE;
}

/// Devuelve la banda de la última captura.
cc1101Band_t rfLastCaptureBand(void) { return rfCapturedBand; }
