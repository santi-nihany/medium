//===----------------------------------------------------------------------===//
///
/// \file
/// Captura y reproducción de tramas RF con CC1101.
///
//===----------------------------------------------------------------------===//

#include "modules/rf.h"
#include "drivers/cc1101.h"

/// Timeout máximo para captura completa (ms).
#define RF_CAPTURE_TIMEOUT_MS 5000UL
/// Tiempo de asentamiento de RSSI (us).
#define RF_RSSI_SETTLE_US 2000UL
/// Cantidad de muestras RSSI para el analizador.
#define RF_RSSI_SAMPLES 12U
/// Muestras para estimar piso de ruido del perfil actual.
#define RF_NOISE_FLOOR_SAMPLES 10U
/// Margen sobre piso de ruido para habilitar trigger de captura.
#define RF_TRIGGER_RSSI_MARGIN_DBM 16
/// Mínimo de muestras consecutivas "sobre umbral" para iniciar captura.
#define RF_TRIGGER_CONSECUTIVE_MIN 10U
/// Período entre muestras de trigger.
#define RF_TRIGGER_SAMPLE_US 150U
/// Tiempo de carrier estable requerido antes de abrir captura.
#define RF_CS_STABLE_US 1500UL
/// Timeout para observar el primer edge tras trigger.
#define RF_EDGE_AFTER_TRIGGER_TIMEOUT_US 30000UL
/// Tiempo máximo sin carrier permitido dentro de una candidata.
#define RF_MAX_CARRIER_LOSS_US 600UL
/// Valor mínimo de piso de ruido permitido (dBm).
#define RF_NOISE_FLOOR_MIN_DBM -110
/// Valor máximo de piso de ruido permitido (dBm).
#define RF_NOISE_FLOOR_MAX_DBM -35

/// Gap que se toma como fin de trama (us).
#define RF_END_GAP_US 12000UL
/// Filtro anti-glitch: pulsos muy cortos se "pegan" al siguiente.
#define RF_GLITCH_FILTER_US 80UL
/// Duración mínima de pulso válido para filtrar ruido (us).
#define RF_MIN_PULSE_US 170UL
/// Duración máxima de pulso aceptado (us).
#define RF_MAX_PULSE_US 30000UL
/// Cantidad mínima de pulsos para considerar captura válida.
#define RF_MIN_VALID_PULSES 40U
/// Duración mínima total de trama válida (us).
#define RF_MIN_VALID_TOTAL_US 18000UL
/// Duración máxima total de trama válida (us).
#define RF_MAX_VALID_TOTAL_US 2500000UL
/// Pulso mínimo para considerar que hubo variación real en la trama.
#define RF_MIN_PEAK_PULSE_US 700UL
/// Cantidad mínima de pulsos largos para validar trama real.
#define RF_MIN_LONG_PULSES 10U
/// Umbral de pulso "largo".
#define RF_LONG_PULSE_US 750UL
/// Ratio máximo de pulsos cortos permitido.
#define RF_MAX_SHORT_PULSE_RATIO_PCT 45U
/// Umbral para considerar pulso "corto" en validación.
#define RF_SHORT_PULSE_US 260UL

/// Duración máxima permitida por edge al reproducir desde .sig.
#define RF_REPLAY_MAX_EDGE_US 30000UL
/// Duración total máxima permitida al reproducir desde .sig.
#define RF_REPLAY_MAX_TOTAL_US 800000UL
/// Parámetros de decoder Princeton.
#define RF_PRINCETON_TE_SHORT_US 390U
#define RF_PRINCETON_TE_LONG_US 1170U
#define RF_PRINCETON_TE_DELTA_US 300U
#define RF_PRINCETON_PREAMBLE_TE 36U
#define RF_PRINCETON_GUARD_DEFAULT 30U

typedef struct {
  uint32_t frequencyHz;
  cc1101ModPreset_t preset;
} rfProfile_t;

static uint16_t rfPulseUs[RF_CAPTURE_PULSES_MAX];
static uint16_t rfPulseCount = 0;
static bool_t rfFirstLevel = FALSE;
static bool_t rfCaptureValid = FALSE;
static cc1101OokConfig_t rfActiveConfig = {0};
static cc1101OokConfig_t rfCapturedConfig = {0};
static uint32_t rfPreferredFrequencyHz = 433920000UL;
static cc1101ModPreset_t rfPreferredPreset = CC1101_OOK_PRESET_AM650_ASYNC;

static const rfProfile_t rfAllProfiles[] = {
    {433920000UL, CC1101_OOK_PRESET_AM650_ASYNC},
    {433920000UL, CC1101_OOK_PRESET_AM270_ASYNC},
    {315000000UL, CC1101_OOK_PRESET_AM650_ASYNC},
    {315000000UL, CC1101_OOK_PRESET_AM270_ASYNC},
};

#define RF_PROFILE_COUNT (sizeof(rfAllProfiles) / sizeof(rfAllProfiles[0]))

/// Diferencia absoluta entre dos enteros sin signo.
static uint32_t rfAbsDiffU32(uint32_t a, uint32_t b) {
  return (a >= b) ? (a - b) : (b - a);
}

/// Condición |a-b| < delta
static bool_t rfDurationDiffLess(uint32_t a, uint32_t b, uint32_t delta) {
  return (rfAbsDiffU32(a, b) < delta) ? TRUE : FALSE;
}

/// Decoder Princeton state-machine sobre un stream nivel/duración.
static bool_t rfDecodePrincetonFromLevelStream(const uint16_t *pulsesUs,
                                               uint16_t count,
                                               bool_t firstLevel,
                                               rfPrincetonInfo_t *infoOut) {
  uint8_t parserStep = 0U; // 0 reset, 1 save, 2 check
  uint32_t teLast = 0U;
  uint32_t decodeData = 0U;
  uint8_t decodeCountBit = 0U;
  uint32_t lastData = 0U;
  uint32_t teAcc = 0U;
  uint32_t guardTime = RF_PRINCETON_GUARD_DEFAULT;
  bool_t level = firstLevel;

  if (pulsesUs == NULL || infoOut == NULL || count == 0U) {
    return FALSE;
  }

  for (uint16_t i = 0U; i < count; i++) {
    uint32_t duration = pulsesUs[i];

    switch (parserStep) {
    case 0U: // Reset
      if ((!level) &&
          rfDurationDiffLess(
              duration, RF_PRINCETON_TE_SHORT_US * RF_PRINCETON_PREAMBLE_TE,
              RF_PRINCETON_TE_DELTA_US * RF_PRINCETON_PREAMBLE_TE)) {
        parserStep = 1U;
        decodeData = 0U;
        decodeCountBit = 0U;
        teAcc = 0U;
        guardTime = RF_PRINCETON_GUARD_DEFAULT;
      }
      break;

    case 1U: // SaveDuration (espera nivel alto)
      if (level) {
        teLast = duration;
        teAcc += duration;
        parserStep = 2U;
      }
      break;

    default: // CheckDuration (espera nivel bajo)
      if (!level) {
        if (duration >= (RF_PRINCETON_TE_LONG_US * 2U)) {
          parserStep = 1U;
          if (decodeCountBit == RF_PRINCETON_BITS) {
            if ((lastData == decodeData) && (lastData != 0U)) {
              uint32_t teUs = teAcc / (uint32_t)(decodeCountBit * 4U + 1U);
              guardTime = (teUs > 0U) ? ((duration + (teUs / 2U)) / teUs)
                                      : RF_PRINCETON_GUARD_DEFAULT;
              if (guardTime < 15U || guardTime > 72U) {
                guardTime = RF_PRINCETON_GUARD_DEFAULT;
              }

              infoOut->key = decodeData & 0x00FFFFFFUL;
              infoOut->bitCount = decodeCountBit;
              infoOut->teUs = (teUs > 0xFFFFUL) ? 0xFFFFU : (uint16_t)teUs;
              infoOut->guardTime = (uint8_t)guardTime;
              return TRUE;
            }
            lastData = decodeData;
          }
          decodeData = 0U;
          decodeCountBit = 0U;
          teAcc = 0U;
          break;
        }

        teAcc += duration;

        if (rfDurationDiffLess(teLast, RF_PRINCETON_TE_SHORT_US,
                               RF_PRINCETON_TE_DELTA_US) &&
            rfDurationDiffLess(duration, RF_PRINCETON_TE_LONG_US,
                               RF_PRINCETON_TE_DELTA_US * 3U)) {
          decodeData = (decodeData << 1) | 0U;
          if (decodeCountBit < 32U) {
            decodeCountBit++;
          }
          parserStep = 1U;
        } else if (rfDurationDiffLess(teLast, RF_PRINCETON_TE_LONG_US,
                                      RF_PRINCETON_TE_DELTA_US * 3U) &&
                   rfDurationDiffLess(duration, RF_PRINCETON_TE_SHORT_US,
                                      RF_PRINCETON_TE_DELTA_US)) {
          decodeData = (decodeData << 1) | 1U;
          if (decodeCountBit < 32U) {
            decodeCountBit++;
          }
          parserStep = 1U;
        } else {
          parserStep = 0U;
        }
      } else {
        parserStep = 0U;
      }
      break;
    }

    level = !level;
  }

  return FALSE;
}

/// Convierte ciclos de clock a microsegundos.
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

/// Convierte RSSI crudo del CC1101 a dBm aproximados.
static int16_t rfReadRssiDbm(void) {
  int16_t raw = (int16_t)cc1101_readRegister(CC1101_RSSI);
  if (raw >= 128) {
    raw -= 256;
  }
  return (int16_t)(raw / 2 - 74);
}

/// Indica si Carrier Sense está activo según PKTSTATUS[6].
static bool_t rfCarrierSenseActive(void) {
  return ((cc1101_readRegister(CC1101_PKTSTATUS) & (1U << 6)) != 0U) ? TRUE
                                                                     : FALSE;
}

/// Crea configuración OOK para una frecuencia/preset.
static cc1101OokConfig_t rfBuildConfig(uint32_t frequencyHz,
                                       cc1101ModPreset_t preset) {
  cc1101OokConfig_t config;
  config.band = (rfAbsDiffU32(frequencyHz, 315000000UL) <
                 rfAbsDiffU32(frequencyHz, 433920000UL))
                    ? CC1101_BAND_315MHZ
                    : CC1101_BAND_433MHZ;
  config.frequencyHz = frequencyHz;
  config.preset = preset;
  config.paTable = (config.band == CC1101_BAND_315MHZ)
                       ? CC1101_OOK_PA_TABLE_315
                       : CC1101_OOK_PA_TABLE_433;
  config.paTableSize = 8;
  return config;
}

/// Aplica una configuración y deja el CC1101 en RX.
static bool_t rfApplyConfig(const cc1101OokConfig_t *config) {
  if (!cc1101_applyOokConfig(config)) {
    return FALSE;
  }
  if (!cc1101_enterRx()) {
    return FALSE;
  }
  rfActiveConfig = *config;
  return TRUE;
}

/// Mide score de una configuración para el analizador rápido.
static bool_t rfMeasureProfile(const cc1101OokConfig_t *config,
                               int32_t *scoreOut) {
  int32_t sumDbm = 0;
  uint8_t csHits = 0;

  if (scoreOut == NULL || !rfApplyConfig(config)) {
    return FALSE;
  }

  delayInaccurateUs(RF_RSSI_SETTLE_US);

  for (uint8_t i = 0; i < RF_RSSI_SAMPLES; i++) {
    int16_t dbm = rfReadRssiDbm();
    sumDbm += dbm;
    if (rfCarrierSenseActive()) {
      csHits++;
    }
    delayInaccurateUs(250);
  }

  *scoreOut = sumDbm + (int32_t)csHits * 12;
  return TRUE;
}

/// Estima el piso de ruido RSSI del perfil actual.
static int16_t rfEstimateNoiseFloorDbm(void) {
  int32_t sum = 0;

  delayInaccurateUs(RF_RSSI_SETTLE_US);
  for (uint8_t i = 0; i < RF_NOISE_FLOOR_SAMPLES; i++) {
    sum += rfReadRssiDbm();
    delayInaccurateUs(220);
  }

  {
    int16_t avg = (int16_t)(sum / (int32_t)RF_NOISE_FLOOR_SAMPLES);
    if (avg < RF_NOISE_FLOOR_MIN_DBM) {
      avg = RF_NOISE_FLOOR_MIN_DBM;
    }
    if (avg > RF_NOISE_FLOOR_MAX_DBM) {
      avg = RF_NOISE_FLOOR_MAX_DBM;
    }
    return avg;
  }
}

/// Captura una candidata dentro de una ventana de tiempo.
static bool_t rfTryCaptureWindow(uint32_t windowUs,
                                 rfCancelCallback_t cancelCallback,
                                 void *context) {
  uint32_t windowStart = cyclesCounterRead();
  bool_t baseline = cc1101_gdo0Read();
  int16_t noiseFloorDbm = rfEstimateNoiseFloorDbm();
  int16_t triggerDbm = (int16_t)(noiseFloorDbm + RF_TRIGGER_RSSI_MARGIN_DBM);
  uint8_t triggerConsecutive = 0U;

  while (rfCyclesToUs(cyclesCounterRead() - windowStart) < windowUs) {
    uint32_t startEdgeCycles;
    uint32_t lastEdgeCycles;
    uint32_t lastCarrierCycles;
    bool_t lastLevel;
    uint16_t pulseCount = 0U;
    uint32_t totalUs = 0U;
    uint16_t maxPulseUs = 0U;
    uint16_t shortPulseCount = 0U;
    uint16_t longPulseCount = 0U;
    uint16_t veryLongPulseCount = 0U;
    uint32_t pendingGlitchUs = 0U;
    bool_t sawCarrier = FALSE;
    uint16_t carrierHitCount = 0U;

    if (cancelCallback != NULL && cancelCallback(context)) {
      return FALSE;
    }

    {
      bool_t level = cc1101_gdo0Read();
      int16_t rssiDbm = rfReadRssiDbm();
      bool_t triggerReady = FALSE;

      if (rfCarrierSenseActive() || rssiDbm >= triggerDbm) {
        if (triggerConsecutive < 255U) {
          triggerConsecutive++;
        }
      } else if (triggerConsecutive > 0U) {
        triggerConsecutive--;
      }

      if (triggerConsecutive >= RF_TRIGGER_CONSECUTIVE_MIN &&
          level != baseline) {
        triggerReady = TRUE;
      }

      if (!triggerReady) {
        delayInaccurateUs(RF_TRIGGER_SAMPLE_US);
        continue;
      }

      // Exigir carrier estable antes de abrir candidata.
      {
        uint32_t csStart = cyclesCounterRead();
        while (rfCyclesToUs(cyclesCounterRead() - csStart) < RF_CS_STABLE_US) {
          if (cancelCallback != NULL && cancelCallback(context)) {
            return FALSE;
          }
          if (!rfCarrierSenseActive()) {
            triggerConsecutive = 0U;
            triggerReady = FALSE;
            break;
          }
          delayInaccurateUs(80);
        }
      }
      if (!triggerReady) {
        continue;
      }

      rfFirstLevel = level;
      lastLevel = level;
      startEdgeCycles = cyclesCounterRead();
      lastEdgeCycles = startEdgeCycles;
      lastCarrierCycles = startEdgeCycles;
      baseline = level;

      // Evitar abrir candidata sin edge inmediato real.
      {
        bool_t firstEdgeOk = FALSE;
        while (rfCyclesToUs(cyclesCounterRead() - startEdgeCycles) <
               RF_EDGE_AFTER_TRIGGER_TIMEOUT_US) {
          bool_t nowLevel = cc1101_gdo0Read();
          if (cancelCallback != NULL && cancelCallback(context)) {
            return FALSE;
          }
          if (rfCarrierSenseActive()) {
            lastCarrierCycles = cyclesCounterRead();
            sawCarrier = TRUE;
            carrierHitCount++;
          }
          if (nowLevel != lastLevel) {
            firstEdgeOk = TRUE;
            break;
          }
          if (rfCyclesToUs(cyclesCounterRead() - lastCarrierCycles) >
              RF_MAX_CARRIER_LOSS_US) {
            break;
          }
          delayInaccurateUs(60);
        }
        if (!firstEdgeOk) {
          triggerConsecutive = 0U;
          continue;
        }
      }
    }

    while (rfCyclesToUs(cyclesCounterRead() - windowStart) < windowUs &&
           pulseCount < RF_CAPTURE_PULSES_MAX) {
      bool_t level = cc1101_gdo0Read();
      uint32_t nowCycles = cyclesCounterRead();

      if (cancelCallback != NULL && cancelCallback(context)) {
        return FALSE;
      }

      if (level != lastLevel) {
        uint32_t dtUs = rfCyclesToUs(nowCycles - lastEdgeCycles);
        lastEdgeCycles = nowCycles;
        lastLevel = level;
        if (rfCarrierSenseActive()) {
          sawCarrier = TRUE;
          lastCarrierCycles = nowCycles;
        }

        if (dtUs <= RF_GLITCH_FILTER_US) {
          if (pendingGlitchUs <= (0xFFFFFFFFUL - dtUs)) {
            pendingGlitchUs += dtUs;
          } else {
            pendingGlitchUs = 0xFFFFFFFFUL;
          }
          continue;
        }

        if (pendingGlitchUs > 0U) {
          if (dtUs <= (0xFFFFFFFFUL - pendingGlitchUs)) {
            dtUs += pendingGlitchUs;
          }
          pendingGlitchUs = 0U;
        }

        if (dtUs < RF_MIN_PULSE_US) {
          continue;
        }
        if (dtUs > RF_MAX_PULSE_US) {
          break;
        }

        rfPulseUs[pulseCount++] = (uint16_t)dtUs;
        totalUs += dtUs;
        if (dtUs > maxPulseUs) {
          maxPulseUs = (uint16_t)dtUs;
        }
        if (dtUs <= RF_SHORT_PULSE_US) {
          shortPulseCount++;
        }
        if (dtUs >= RF_LONG_PULSE_US) {
          longPulseCount++;
        }
        if (dtUs >= (RF_LONG_PULSE_US + 250UL)) {
          veryLongPulseCount++;
        }

        if (totalUs > RF_MAX_VALID_TOTAL_US) {
          break;
        }
      } else {
        uint32_t idleUs = rfCyclesToUs(nowCycles - lastEdgeCycles);
        if (pulseCount >= 6U && idleUs > RF_END_GAP_US) {
          break;
        }
        if (rfCarrierSenseActive()) {
          sawCarrier = TRUE;
          lastCarrierCycles = nowCycles;
          carrierHitCount++;
        } else if (rfCyclesToUs(nowCycles - lastCarrierCycles) >
                   RF_MAX_CARRIER_LOSS_US) {
          break;
        }
        delayInaccurateUs(30);
      }
    }

    if (pulseCount >= RF_MIN_VALID_PULSES && totalUs >= RF_MIN_VALID_TOTAL_US &&
        totalUs <= RF_MAX_VALID_TOTAL_US &&
        maxPulseUs >= RF_MIN_PEAK_PULSE_US && sawCarrier &&
        longPulseCount >= RF_MIN_LONG_PULSES && veryLongPulseCount >= 2U &&
        carrierHitCount >= 8U &&
        ((uint32_t)shortPulseCount * 100UL) <=
            ((uint32_t)pulseCount * (uint32_t)RF_MAX_SHORT_PULSE_RATIO_PCT)) {
      rfPulseCount = pulseCount;
      rfCaptureValid = TRUE;
#ifdef MEDIUM_DEBUG
      printf(
          "[modules] [rf] Captura OK: %u pulsos, nivel inicial=%u, floor=%ddBm "
          "thr=%ddBm long=%u vlong=%u short=%u cs=%u\r\n",
          rfPulseCount, rfFirstLevel, (int)noiseFloorDbm, (int)triggerDbm,
          (unsigned)longPulseCount, (unsigned)veryLongPulseCount,
          (unsigned)shortPulseCount, (unsigned)carrierHitCount);
#endif
      return TRUE;
    }
  }

  return FALSE;
}

bool_t rfInit(void) {
  cc1101OokConfig_t config =
      rfBuildConfig(433920000UL, CC1101_OOK_PRESET_AM650_ASYNC);

  if (!cc1101_init()) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [rf] ERROR: cc1101_init() fallo\r\n");
#endif
    return FALSE;
  }

  rfCaptureValid = FALSE;
  rfPreferredFrequencyHz = config.frequencyHz;
  rfPreferredPreset = config.preset;

  if (!rfApplyConfig(&config)) {
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] RF listo (433.920MHz, AM650)\r\n");
#endif
  return TRUE;
}

bool_t rfSetCaptureConfig(const cc1101OokConfig_t *config) {
  if (config == NULL) {
    return FALSE;
  }

  rfPreferredFrequencyHz = config->frequencyHz;
  rfPreferredPreset = config->preset;
  return rfApplyConfig(config);
}

bool_t rfCaptureWithCancel(rfCancelCallback_t cancelCallback, void *context) {
  cc1101OokConfig_t config =
      rfBuildConfig(rfPreferredFrequencyHz, rfPreferredPreset);

  rfCaptureValid = FALSE;

  if (!rfApplyConfig(&config)) {
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] Captura RF %lu.%03luMHz %s\r\n",
         (unsigned long)(config.frequencyHz / 1000000UL),
         (unsigned long)((config.frequencyHz % 1000000UL) / 1000UL),
         (config.preset == CC1101_OOK_PRESET_AM270_ASYNC) ? "AM270" : "AM650");
#endif

  if (rfTryCaptureWindow(RF_CAPTURE_TIMEOUT_MS * 1000UL, cancelCallback,
                         context)) {
    rfCapturedConfig = config;
    rfActiveConfig = config;
    return TRUE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] ERROR: timeout esperando señal RF\r\n");
#endif
  return FALSE;
}

bool_t rfCapture(void) { return rfCaptureWithCancel(NULL, NULL); }

bool_t rfRunFrequencyAnalyzer(void) {
  int32_t bestScore = -2147483647;
  cc1101OokConfig_t bestConfig =
      rfBuildConfig(433920000UL, CC1101_OOK_PRESET_AM650_ASYNC);

  for (uint8_t i = 0; i < RF_PROFILE_COUNT; i++) {
    cc1101OokConfig_t config =
        rfBuildConfig(rfAllProfiles[i].frequencyHz, rfAllProfiles[i].preset);
    int32_t score = -2147483647;

    if (!rfMeasureProfile(&config, &score)) {
      continue;
    }

    if (score > bestScore) {
      bestScore = score;
      bestConfig = config;
    }
  }

  rfPreferredFrequencyHz = bestConfig.frequencyHz;
  rfPreferredPreset = bestConfig.preset;

  if (!rfApplyConfig(&bestConfig)) {
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] Analizador: %lu.%03luMHz %s (score=%ld)\r\n",
         (unsigned long)(bestConfig.frequencyHz / 1000000UL),
         (unsigned long)((bestConfig.frequencyHz % 1000000UL) / 1000UL),
         (bestConfig.preset == CC1101_OOK_PRESET_AM270_ASYNC) ? "AM270"
                                                              : "AM650",
         (long)bestScore);
#endif
  return TRUE;
}

bool_t rfReplayCaptured(void) {
  bool_t level;

  if (!rfCaptureValid || rfPulseCount == 0) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [rf] ERROR: no hay captura valida para reproducir\r\n");
#endif
    return FALSE;
  }

  if (!rfApplyConfig(&rfCapturedConfig)) {
    return FALSE;
  }

  cc1101_enterIdle();
  gpioInit(CC1101_GDO0_PIN, GPIO_OUTPUT);
  level = rfFirstLevel;
  gpioWrite(CC1101_GDO0_PIN, level);

  if (!cc1101_enterTx()) {
    gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
    return FALSE;
  }

  __disable_irq();
  for (uint16_t i = 0; i < rfPulseCount; i++) {
    delayInaccurateUs(rfPulseUs[i]);
    level = !level;
    gpioWrite(CC1101_GDO0_PIN, level);
  }
  delayInaccurateUs(2000);
  __enable_irq();

  cc1101_enterIdle();
  gpioWrite(CC1101_GDO0_PIN, FALSE);
  gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
  cc1101_enterRx();

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] Replay OK: %u pulsos\r\n", rfPulseCount);
#endif
  return TRUE;
}

bool_t rfReplayEdges(const uint32_t *edges, uint32_t edgeCount,
                     uint8_t startLevel, int8_t tickScale) {
  bool_t level = startLevel ? TRUE : FALSE;
  uint32_t totalUs = 0U;
  bool_t ok = TRUE;

  if (edges == NULL || edgeCount == 0U || edgeCount > RF_CAPTURE_PULSES_MAX) {
    return FALSE;
  }

  if (!rfApplyConfig(&rfActiveConfig)) {
    return FALSE;
  }

  cc1101_enterIdle();
  gpioInit(CC1101_GDO0_PIN, GPIO_OUTPUT);
  gpioWrite(CC1101_GDO0_PIN, level);

  if (!cc1101_enterTx()) {
    gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
    return FALSE;
  }

  __disable_irq();
  for (uint32_t i = 0; i < edgeCount; i++) {
    uint32_t durationUs = rfTicksToUs(edges[i], tickScale);
    if (durationUs == 0U) {
      durationUs = 1U;
    }
    if (durationUs > RF_REPLAY_MAX_EDGE_US) {
      ok = FALSE;
      break;
    }
    if (totalUs > (RF_REPLAY_MAX_TOTAL_US - durationUs)) {
      ok = FALSE;
      break;
    }
    totalUs += durationUs;
    delayInaccurateUs(durationUs);
    level = !level;
    gpioWrite(CC1101_GDO0_PIN, level);
  }

  if (ok) {
    delayInaccurateUs(2000);
  }
  __enable_irq();
  cc1101_enterIdle();
  gpioWrite(CC1101_GDO0_PIN, FALSE);
  gpioInit(CC1101_GDO0_PIN, GPIO_INPUT);
  cc1101_enterRx();

  if (!ok) {
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [rf] Replay desde .sig OK (%lu edges)\r\n",
         (unsigned long)edgeCount);
#endif
  return TRUE;
}

bool_t rfDecodePrinceton(const uint16_t *pulsesUs, uint16_t count,
                         bool_t firstLevel, rfPrincetonInfo_t *infoOut) {
  if (infoOut == NULL) {
    return FALSE;
  }
  if (rfDecodePrincetonFromLevelStream(pulsesUs, count, firstLevel, infoOut)) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [rf] Princeton decode OK key=0x%06lX te=%uus gt=%u "
           "bits=%u\r\n",
           (unsigned long)infoOut->key, (unsigned)infoOut->teUs,
           (unsigned)infoOut->guardTime, (unsigned)infoOut->bitCount);
#endif
    return TRUE;
  }
  // Si el nivel inicial llegó invertido por captura, probamos invertido.
  if (rfDecodePrincetonFromLevelStream(pulsesUs, count, !firstLevel, infoOut)) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [rf] Princeton decode OK(inv) key=0x%06lX te=%uus gt=%u "
           "bits=%u\r\n",
           (unsigned long)infoOut->key, (unsigned)infoOut->teUs,
           (unsigned)infoOut->guardTime, (unsigned)infoOut->bitCount);
#endif
    return TRUE;
  }
  return FALSE;
}

bool_t rfDecodeLastPrinceton(rfPrincetonInfo_t *infoOut) {
  const uint16_t *pulsesUs = NULL;
  uint16_t count = 0U;
  bool_t firstLevel = FALSE;

  if (!rfGetLastCapture(&pulsesUs, &count, &firstLevel)) {
    return FALSE;
  }

  return rfDecodePrinceton(pulsesUs, count, firstLevel, infoOut);
}

bool_t rfHasCapture(void) { return rfCaptureValid; }

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

bool_t rfGetActiveConfig(cc1101OokConfig_t *config) {
  if (config == NULL) {
    return FALSE;
  }
  *config = rfActiveConfig;
  return TRUE;
}

bool_t rfGetLastCaptureConfig(cc1101OokConfig_t *config) {
  if (config == NULL || !rfCaptureValid) {
    return FALSE;
  }
  *config = rfCapturedConfig;
  return TRUE;
}
