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
/// Gap que se toma como fin de trama (us).
#define RF_END_GAP_US 12000
/// Duración mínima de pulso válido para filtrar ruido (us).
#define RF_MIN_PULSE_US 120
/// Cantidad mínima de pulsos para considerar una captura válida.
#define RF_MIN_VALID_PULSES 10
/// Cantidad máxima de pulsos para aceptar una captura válida.
#define RF_MAX_VALID_PULSES 320
/// Duración mínima total de trama válida (us).
#define RF_MIN_VALID_TOTAL_US 5000UL
/// Duración máxima total de trama válida (us).
#define RF_MAX_VALID_TOTAL_US 2500000UL
/// Tiempo mínimo de carrier-sense estable antes de capturar (us).
#define RF_CS_STABLE_US 1500UL
/// Umbral absoluto de Carrier Sense (AGCCTRL1[5:4]): 0..3.
/// 1 es el comportamiento "normal" actual.
#define RF_CS_ABS_THR_BITS 1U
/// Timeout para ver el primer flanco luego de CS estable (us).
#define RF_EDGE_AFTER_CS_TIMEOUT_US 60000UL
/// Duración máxima permitida por edge al reproducir desde .sig.
#define RF_REPLAY_MAX_EDGE_US 30000UL
/// Duración total máxima permitida al reproducir desde .sig.
#define RF_REPLAY_MAX_TOTAL_US 800000UL

static uint16_t rfPulseUs[RF_CAPTURE_PULSES_MAX];
static uint16_t rfPulseCount = 0;
static bool_t rfFirstLevel = FALSE;
static bool_t rfCaptureValid = FALSE;
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

/// Aplica umbral absoluto de carrier sense fijo.
static bool_t rfApplyCarrierSenseThreshold(void) {
  uint8_t agc1 = cc1101_readRegister(CC1101_AGCCTRL1);
  uint8_t csAbsThrBits = (uint8_t)(RF_CS_ABS_THR_BITS & 0x3U);
  agc1 =
      (uint8_t)((agc1 & (uint8_t)~(0x3U << 4)) | (uint8_t)(csAbsThrBits << 4));
  if (!cc1101_writeRegister(CC1101_AGCCTRL1, agc1)) {
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] CS thr bits=%u (AGCCTRL1=0x%02X)\r\n",
         (unsigned)csAbsThrBits, agc1);
#endif
  return TRUE;
}

/// Indica si Carrier Sense está activo según PKTSTATUS[6].
static bool_t rfCarrierSenseActive(void) {
  return ((cc1101_readRegister(CC1101_PKTSTATUS) & (1U << 6)) != 0U) ? TRUE
                                                                     : FALSE;
}

/// Aplica la configuración de captura actual.
static bool_t rfApplyCurrentProfile(void) {
  bool_t ok = cc1101_applyOokConfig(&rfCaptureConfig);
  if (ok) {
    ok = rfApplyCarrierSenseThreshold();
  }
#ifdef MEDIUM_DEBUG
  if (ok) {
    printf("[modules] [rf] Perfil 433MHz aplicado (BW=%luHz)\r\n",
           (unsigned long)rfCaptureConfig.rxBandwidthHz);
  } else {
    printf("[modules] [rf] ERROR: no se pudo aplicar perfil 433MHz\r\n");
  }
#endif
  return ok;
}

/// Carga parámetros de captura fijos para 433 MHz.
static void rfLoadDefaultConfig433(void) {
  rfCaptureConfig = CC1101_OOK_CONFIG_433;
  rfCaptureConfig.addressCheckEnable = FALSE;
  rfCaptureConfig.crcEnable = FALSE;
  rfCaptureConfig.variableLength = FALSE;
  rfCaptureConfig.packetLength = 0xFF;
  rfCaptureConfig.rxBandwidthHz = 101000;
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

  rfLoadDefaultConfig433();
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
  // Forzar operación exclusiva en 433 MHz.
  rfCaptureConfig.band = CC1101_BAND_433MHZ;
  rfCaptureConfig.paTable = CC1101_OOK_PA_TABLE_433;
  rfCaptureConfig.paTableSize = 2;
  return rfApplyCurrentProfile();
}

/// Ajusta sensibilidad de captura RF (carrier sense threshold).
/// Captura una trama recibida y la guarda como secuencia de pulsos.
bool_t rfCaptureWithCancel(rfCancelCallback_t cancelCallback, void *context) {
  uint32_t globalStartCycles;

  if (!rfApplyCurrentProfile()) {
    return FALSE;
  }

  cc1101_enterRx();

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] Capturando señal en 433MHz...\r\n");
#endif

  globalStartCycles = cyclesCounterRead();

  while (rfCyclesToUs(cyclesCounterRead() - globalStartCycles) <
         (RF_CAPTURE_TIMEOUT_MS * 1000UL)) {
    bool_t baseLevel = cc1101_gdo0Read();
    uint32_t candidateStartCycles;
    uint32_t lastEdgeCycles;
    uint32_t totalUs = 0U;
    uint16_t pulseCount = 0U;
    bool_t level;
    bool_t lastLevel;
    bool_t overflow = FALSE;
    bool_t csStable = FALSE;
    bool_t edgeDetected = FALSE;

    // 1) Esperar CS estable.
    while (rfCyclesToUs(cyclesCounterRead() - globalStartCycles) <
           (RF_CAPTURE_TIMEOUT_MS * 1000UL)) {
      uint32_t csStartCycles;
      if (cancelCallback != NULL && cancelCallback(context)) {
#ifdef MEDIUM_DEBUG
        printf("[modules] [rf] Captura cancelada por callback\r\n");
#endif
        return FALSE;
      }
      if (!rfCarrierSenseActive()) {
        delayInaccurateUs(200);
        continue;
      }

      csStartCycles = cyclesCounterRead();
      while (rfCarrierSenseActive()) {
        if (cancelCallback != NULL && cancelCallback(context)) {
#ifdef MEDIUM_DEBUG
          printf("[modules] [rf] Captura cancelada por callback\r\n");
#endif
          return FALSE;
        }
        if (rfCyclesToUs(cyclesCounterRead() - csStartCycles) >=
            RF_CS_STABLE_US) {
          csStable = TRUE;
          break;
        }
        delayInaccurateUs(100);
      }

      if (csStable) {
        break;
      }
    }

    if (!csStable || rfCyclesToUs(cyclesCounterRead() - globalStartCycles) >=
                         (RF_CAPTURE_TIMEOUT_MS * 1000UL)) {
      break;
    }

    // 2) CS estable: esperar primer flanco en ventana corta.
    baseLevel = cc1101_gdo0Read();
    candidateStartCycles = cyclesCounterRead();
    while (rfCyclesToUs(cyclesCounterRead() - candidateStartCycles) <
           RF_EDGE_AFTER_CS_TIMEOUT_US) {
      bool_t cur = cc1101_gdo0Read();
      if (cancelCallback != NULL && cancelCallback(context)) {
#ifdef MEDIUM_DEBUG
        printf("[modules] [rf] Captura cancelada por callback\r\n");
#endif
        return FALSE;
      }
      if (cur != baseLevel) {
        edgeDetected = TRUE;
        break;
      }
      if (!rfCarrierSenseActive()) {
        break;
      }
      delayInaccurateUs(120);
    }

    if (!edgeDetected) {
      continue;
    }

    // 3) Capturar candidata.
    rfCaptureValid = FALSE;
    rfFirstLevel = !baseLevel;
    lastLevel = rfFirstLevel;
    candidateStartCycles = cyclesCounterRead();
    lastEdgeCycles = candidateStartCycles;

    while (pulseCount < RF_CAPTURE_PULSES_MAX) {
      uint32_t nowCycles = cyclesCounterRead();
      level = cc1101_gdo0Read();

      if (cancelCallback != NULL && cancelCallback(context)) {
#ifdef MEDIUM_DEBUG
        printf("[modules] [rf] Captura cancelada por callback\r\n");
#endif
        return FALSE;
      }

      if (level != lastLevel) {
        uint32_t dtUs = rfCyclesToUs(nowCycles - lastEdgeCycles);
        if (dtUs < RF_MIN_PULSE_US) {
          lastEdgeCycles = nowCycles;
          lastLevel = level;
          continue;
        }
        if (dtUs > 0xFFFFUL) {
          dtUs = 0xFFFFUL;
        }
        rfPulseUs[pulseCount++] = (uint16_t)dtUs;
        if (totalUs > (0xFFFFFFFFUL - dtUs)) {
          totalUs = 0xFFFFFFFFUL;
        } else {
          totalUs += dtUs;
        }
        lastEdgeCycles = nowCycles;
        lastLevel = level;
      } else {
        uint32_t idleUs = rfCyclesToUs(nowCycles - lastEdgeCycles);
        uint32_t frameUs = rfCyclesToUs(nowCycles - candidateStartCycles);
        if (idleUs > RF_END_GAP_US && pulseCount > 4U) {
          break;
        }
        if (frameUs > RF_MAX_VALID_TOTAL_US) {
          break;
        }
      }

      if (rfCyclesToUs(cyclesCounterRead() - globalStartCycles) >=
          (RF_CAPTURE_TIMEOUT_MS * 1000UL)) {
        break;
      }
    }

    if (pulseCount >= RF_CAPTURE_PULSES_MAX) {
      overflow = TRUE;
    }

    if (overflow || pulseCount < RF_MIN_VALID_PULSES ||
        pulseCount > RF_MAX_VALID_PULSES || totalUs < RF_MIN_VALID_TOTAL_US ||
        totalUs > RF_MAX_VALID_TOTAL_US) {
#ifdef MEDIUM_DEBUG
      if (overflow) {
        printf(
            "[modules] [rf] WARN: candidata descartada por overflow (%u)\r\n",
            RF_CAPTURE_PULSES_MAX);
      } else {
        printf("[modules] [rf] WARN: candidata descartada (pulsos=%u "
               "total=%luus)\r\n",
               pulseCount, (unsigned long)totalUs);
      }
#endif
      continue;
    }

    rfPulseCount = pulseCount;
    rfCaptureValid = TRUE;
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

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] ERROR: timeout esperando inicio de señal\r\n");
#endif
  return FALSE;
}

/// Captura una trama RF.
bool_t rfCapture(void) { return rfCaptureWithCancel(NULL, NULL); }

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
  printf("[modules] [rf] Replay cfg: band=433MHz bw=%lu rate=%lu async=%u "
         "iocfg0=0x%02X pktctrl0=0x%02X\r\n",
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
  delayInaccurateUs(2000);

  cc1101_enterIdle();
  gpioWrite(CC1101_GDO0_PIN, FALSE);
  gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
  cc1101_enterRx();

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] Replay OK: %u pulsos enviados (433MHz)\r\n",
         rfPulseCount);
#endif

  return TRUE;
}

/// Reproduce una señal RF desde flancos cargados de un .sig.
bool_t rfReplayEdges(const uint32_t *edges, uint32_t edgeCount,
                     uint8_t startLevel, int8_t tickScale) {
  bool_t level = startLevel ? TRUE : FALSE;
  uint32_t totalUs = 0U;

  if (edges == NULL || edgeCount == 0U || edgeCount > RF_CAPTURE_PULSES_MAX) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [rf] ERROR: rfReplayEdges sin datos validos\r\n");
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
  delayInaccurateUs(2000);

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
