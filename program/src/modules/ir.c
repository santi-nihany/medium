//===----------------------------------------------------------------------===//
///
/// \file
/// Captura, reproducción y codificación NEC para IR.
///
//===----------------------------------------------------------------------===//

#include "modules/ir.h"
#include "drivers/ir_leds.h"

/// Timeout máximo sin flancos para cerrar trama.
#define IR_FRAME_TIMEOUT_US 40000
/// Espera máxima de inicio de trama.
#define IR_START_TIMEOUT_US 3000000
/// Duración mínima de pulso válida.
#define IR_MIN_PULSE_US 300
/// Paso de muestreo en captura.
#define IR_SAMPLE_US 30
/// Frecuencia del carrier IR.
#define IR_CARRIER_FREQ_HZ 38000
/// Medio período del carrier.
#define IR_CARRIER_HALF_US (1000000 / (IR_CARRIER_FREQ_HZ * 2))
/// Duración máxima permitida por edge al reproducir desde .sig.
#define IR_REPLAY_MAX_EDGE_US 200000UL
/// Duración total máxima permitida al reproducir desde .sig.
#define IR_REPLAY_MAX_TOTAL_US 5000000UL

/// Timings NEC en microsegundos.
#define NEC_HDR_MARK_US 9000
#define NEC_HDR_SPACE_US 4500
#define NEC_RPT_SPACE_US 2250
#define NEC_BIT_MARK_US 560
#define NEC_ONE_SPACE_US 1690
#define NEC_ZERO_SPACE_US 560
#define NEC_STOP_MARK_US 560
#define NEC_BITS 32

/// Márgenes de tolerancia NEC.
#define NEC_HDR_MARK_TOL_US 1200
#define NEC_HDR_SPACE_TOL_US 700
#define NEC_RPT_SPACE_TOL_US 500
#define NEC_BIT_MARK_TOL_US 260
#define NEC_BIT_SPACE_TOL_US 360

static IRPulse irPulseBuffer[IR_MAX_PULSES];
static uint16_t irPulseCount = 0;
static bool_t irCaptureValid = FALSE;

/// Estado interno de captura (equivalente a legacy).
static uint8_t irLastLevel = 1;

/// Convierte ciclos de clock a microsegundos.
/// \param cycles cantidad de ciclos
/// \return tiempo en microsegundos
static uint32_t irCyclesToUs(uint32_t cycles) {
  return (uint32_t)(((uint64_t)cycles * 1000000ULL) /
                    (uint64_t)SystemCoreClock);
}

/// Convierte microsegundos a ciclos de clock.
/// \param us tiempo en microsegundos
/// \return cantidad de ciclos
static uint32_t irUsToCycles(uint32_t us) {
  return (uint32_t)(((uint64_t)us * (uint64_t)SystemCoreClock) / 1000000ULL);
}

/// Verifica si un valor cae dentro del rango [min,max].
/// \param value valor a validar
/// \param min limite inferior
/// \param max limite superior
/// \return TRUE si value esta dentro del rango
static bool_t irInRange(uint32_t value, uint32_t min, uint32_t max) {
  return (value >= min && value <= max) ? TRUE : FALSE;
}

/// Calcula 10^exp para exp>=0 con saturación.
static uint32_t irPow10U32(uint8_t exp) {
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
static uint32_t irTicksToUs(uint32_t ticks, int8_t tickScale) {
  if (tickScale >= -6) {
    uint8_t exp = (uint8_t)(tickScale + 6);
    uint32_t mul = irPow10U32(exp);
    if (mul == 0xFFFFFFFFUL) {
      return 0xFFFFFFFFUL;
    }
    if (ticks > (0xFFFFFFFFUL / mul)) {
      return 0xFFFFFFFFUL;
    }
    return ticks * mul;
  } else {
    uint8_t exp = (uint8_t)(-6 - tickScale);
    uint32_t div = irPow10U32(exp);
    if (div == 0U) {
      return 1U;
    }
    ticks = (ticks + (div / 2U)) / div;
    return (ticks == 0U) ? 1U : ticks;
  }
}

/// Lee contador de microsegundos.
static uint32_t irGetTimeUs(void) { return irCyclesToUs(cyclesCounterRead()); }

/// Delay activo en microsegundos usando contador de ciclos.
/// \param us tiempo de espera
static void irBusyDelayUs(uint32_t us) {
  uint32_t startCycles = cyclesCounterRead();
  uint32_t waitCycles = irUsToCycles(us);
  while ((uint32_t)(cyclesCounterRead() - startCycles) < waitCycles) {
  }
}

/// Emite una marca (carrier activo) durante un tiempo.
/// \param durationUs duración en microsegundos
static void irSendMark(uint32_t durationUs) {
  uint32_t start = irGetTimeUs();
  bool_t phase = FALSE;

  while ((irGetTimeUs() - start) < durationUs) {
    phase = !phase;
    irTxWrite(phase);
    irBusyDelayUs(IR_CARRIER_HALF_US);
  }
  irTxOff();
}

/// Emite un espacio (carrier apagado) durante un tiempo.
/// \param durationUs duración en microsegundos
static void irSendSpace(uint32_t durationUs) {
  irTxOff();
  irBusyDelayUs(durationUs);
}

/// Inicializa el módulo IR.
void irInit(void) {
  irLedsInit();
  irPulseCount = 0;
  irCaptureValid = FALSE;
  irLastLevel = 1;

#ifdef MEDIUM_DEBUG
  printf("[modules] [ir] IR inicializado\r\n");
#endif
}

/// Captura una trama IR por polling.
static bool_t irRecordInternal(irCancelCallback_t cancelCallback, void *context,
                               bool_t useStartTimeout) {
  uint32_t startWaitUs;
  uint8_t cur = irRxRead();
  uint8_t startLevel = cur;
  uint32_t lastTime;
  bool_t overflow = FALSE;

  irCaptureValid = FALSE;
  irPulseCount = 0;

#ifdef MEDIUM_DEBUG
  printf("[modules] [ir] Iniciando captura IR...\r\n");
#endif

  startWaitUs = irGetTimeUs();
  while (cur == startLevel) {
    if (cancelCallback != NULL && cancelCallback(context)) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] Captura cancelada por callback\r\n");
#endif
      return FALSE;
    }
    delayInaccurateUs(IR_SAMPLE_US);
    if (useStartTimeout &&
        (irGetTimeUs() - startWaitUs) > IR_START_TIMEOUT_US) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] ERROR: timeout esperando señal IR\r\n");
#endif
      return FALSE;
    }
    cur = irRxRead();
  }

  irLastLevel = cur;
  lastTime = irGetTimeUs();

  while (1) {
    uint8_t level = irRxRead();
    uint32_t now = irGetTimeUs();

    if (cancelCallback != NULL && cancelCallback(context)) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] Captura cancelada por callback\r\n");
#endif
      return FALSE;
    }

    if (level != irLastLevel) {
      uint32_t delta = now - lastTime;
      if (delta >= IR_MIN_PULSE_US) {
        if (irPulseCount < IR_MAX_PULSES && delta < 60000) {
          irPulseBuffer[irPulseCount].durationUs = delta;
          irPulseBuffer[irPulseCount].level = irLastLevel;
          irPulseCount++;
        } else if (irPulseCount >= IR_MAX_PULSES) {
          overflow = TRUE;
          break;
        }
        lastTime = now;
      } else {
        lastTime = now;
      }
      irLastLevel = level;
    }

    if (irPulseCount > 3 && (now - lastTime) > IR_FRAME_TIMEOUT_US) {
      break;
    }

    delayInaccurateUs(IR_SAMPLE_US);
  }

  if (overflow) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [ir] ERROR: overflow de buffer de pulsos (%u)\r\n",
           IR_MAX_PULSES);
#endif
    return FALSE;
  }

  if (irPulseCount == 0) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [ir] ERROR: no se capturaron pulsos IR\r\n");
#endif
    return FALSE;
  }

  irCaptureValid = TRUE;
  irLastLevel = irRxRead();

#ifdef MEDIUM_DEBUG
  printf("[modules] [ir] Captura IR OK: %u pulsos\r\n", irPulseCount);
  printf("[modules] [ir] Primeros pulsos(us): ");
  for (uint16_t i = 0; i < irPulseCount && i < 10; i++) {
    printf("(%u,%lu) ", irPulseBuffer[i].level,
           (unsigned long)irPulseBuffer[i].durationUs);
  }
  printf("\r\n");
#endif

  return TRUE;
}

/// Captura una trama IR por polling (alineado con legacy).
bool_t irRecordWithCancel(irCancelCallback_t cancelCallback, void *context) {
  return irRecordInternal(cancelCallback, context, TRUE);
}

/// Captura sin timeout de inicio; solo se cancela por callback.
bool_t irRecordNoStartTimeoutWithCancel(irCancelCallback_t cancelCallback,
                                        void *context) {
  return irRecordInternal(cancelCallback, context, FALSE);
}

/// Captura una trama IR por polling.
bool_t irRecord(void) { return irRecordWithCancel(NULL, NULL); }

/// Reproduce la última trama IR capturada (0=mark, 1=space).
bool_t irReplay(void) {
  if (!irCaptureValid || irPulseCount == 0) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [ir] ERROR: no hay trama IR para reproducir\r\n");
#endif
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [ir] Reproduciendo trama IR (%u pulsos)\r\n", irPulseCount);
#endif

  __disable_irq();
  for (uint16_t i = 0; i < irPulseCount; i++) {
    if (irPulseBuffer[i].level == 0) {
      irSendMark(irPulseBuffer[i].durationUs);
    } else {
      irSendSpace(irPulseBuffer[i].durationUs);
    }
  }
  __enable_irq();

  irTxOff();

#ifdef MEDIUM_DEBUG
  printf("[modules] [ir] Replay IR OK\r\n");
#endif
  return TRUE;
}

/// Reproduce una secuencia de flancos cargada desde archivo .sig.
bool_t irReplayEdges(const uint32_t *edges, uint32_t edgeCount,
                     uint8_t startLevel, int8_t tickScale) {
  uint8_t level = startLevel;
  uint32_t firstMarkIndex = 0U;
  uint32_t totalUs = 0U;

  if (edges == NULL || edgeCount == 0U) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [ir] ERROR: irReplayEdges sin datos\r\n");
#endif
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [ir] Replay desde .sig: %lu edges, start=%u, scale=%d\r\n",
         (unsigned long)edgeCount, (unsigned)startLevel, (int)tickScale);
#endif

  // Normaliza para arrancar en MARK (nivel 0) y evitar desfase por espacios
  // iniciales capturados antes del frame.
  while (firstMarkIndex < edgeCount && level != 0U) {
    firstMarkIndex++;
    level = (uint8_t)!level;
  }
  if (firstMarkIndex >= edgeCount) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [ir] ERROR: replay sin MARK inicial util\r\n");
#endif
    return FALSE;
  }
#ifdef MEDIUM_DEBUG
  if (firstMarkIndex > 0U) {
    printf("[modules] [ir] WARN: replay IR omitio %lu edges iniciales (space)\r\n",
           (unsigned long)firstMarkIndex);
  }
#endif

  __disable_irq();
  for (uint32_t i = firstMarkIndex; i < edgeCount; i++) {
    uint32_t durationUs = irTicksToUs(edges[i], tickScale);
    if (durationUs == 0U) {
      durationUs = 1U;
    }
    if (durationUs > IR_REPLAY_MAX_EDGE_US) {
      __enable_irq();
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] ERROR: edge demasiado largo (%luus)\r\n",
             (unsigned long)durationUs);
#endif
      irTxOff();
      return FALSE;
    }
    if (totalUs > (IR_REPLAY_MAX_TOTAL_US - durationUs)) {
      __enable_irq();
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] ERROR: replay excede %luus\r\n",
             (unsigned long)IR_REPLAY_MAX_TOTAL_US);
#endif
      irTxOff();
      return FALSE;
    }
    totalUs += durationUs;

    if (level == 0U) {
      irSendMark(durationUs);
    } else {
      irSendSpace(durationUs);
    }
    level = (uint8_t)!level;
  }
  __enable_irq();

  irTxOff();

#ifdef MEDIUM_DEBUG
  printf("[modules] [ir] Replay desde .sig OK\r\n");
#endif
  return TRUE;
}

/// Devuelve la última captura almacenada.
bool_t irGetLastCapture(const IRPulse **pulses, uint16_t *count) {
  if (!irCaptureValid || pulses == NULL || count == NULL) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [ir] WARN: irGetLastCapture sin captura valida\r\n");
#endif
    return FALSE;
  }

  *pulses = irPulseBuffer;
  *count = irPulseCount;
  return TRUE;
}

/// Busca header NEC dentro de la captura, tolerando ruido inicial.
/// \param pulses buffer de pulsos capturados
/// \param count cantidad de pulsos
/// \param headerIndex índice donde inicia el header
/// \param markLevel nivel lógico que representa mark
/// \param spaceLevel nivel lógico que representa space
/// \param repeatFrame TRUE si es frame de repetición NEC
/// \return TRUE si encuentra un header NEC válido
static bool_t irFindNecHeader(const IRPulse *pulses, uint16_t count,
                              uint16_t *headerIndex, uint8_t *markLevel,
                              uint8_t *spaceLevel, bool_t *repeatFrame) {
  if (pulses == NULL || headerIndex == NULL || markLevel == NULL ||
      spaceLevel == NULL || repeatFrame == NULL || count < 2) {
    return FALSE;
  }

  for (uint16_t i = 0; i + 1 < count; i++) {
    uint32_t d0 = pulses[i].durationUs;
    uint32_t d1 = pulses[i + 1].durationUs;
    if (pulses[i].level == pulses[i + 1].level) {
      continue;
    }

    if (!irInRange(d0, NEC_HDR_MARK_US - NEC_HDR_MARK_TOL_US,
                   NEC_HDR_MARK_US + NEC_HDR_MARK_TOL_US)) {
      continue;
    }

    if (irInRange(d1, NEC_HDR_SPACE_US - NEC_HDR_SPACE_TOL_US,
                  NEC_HDR_SPACE_US + NEC_HDR_SPACE_TOL_US)) {
      *headerIndex = i;
      *markLevel = pulses[i].level;
      *spaceLevel = pulses[i + 1].level;
      *repeatFrame = FALSE;
      return TRUE;
    }

    if (irInRange(d1, NEC_RPT_SPACE_US - NEC_RPT_SPACE_TOL_US,
                  NEC_RPT_SPACE_US + NEC_RPT_SPACE_TOL_US)) {
      *headerIndex = i;
      *markLevel = pulses[i].level;
      *spaceLevel = pulses[i + 1].level;
      *repeatFrame = TRUE;
      return TRUE;
    }
  }

  return FALSE;
}

/// Decodifica una trama NEC desde pulsos (alineado con legacy).
bool_t irDecodeNec(const IRPulse *pulses, uint16_t count, uint8_t *address,
                   uint8_t *command) {
  uint16_t headerIndex;
  uint8_t markLevel;
  uint8_t spaceLevel;
  bool_t repeatFrame;
  uint32_t data = 0;

  if (pulses == NULL || address == NULL || command == NULL || count < 4) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [ir] NEC decode fail: parametros invalidos\r\n");
#endif
    return FALSE;
  }

  if (!irFindNecHeader(pulses, count, &headerIndex, &markLevel, &spaceLevel,
                       &repeatFrame)) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [ir] NEC decode fail: header no encontrado\r\n");
#endif
    return FALSE;
  }

  if (repeatFrame) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [ir] NEC repeat frame detectado (sin datos)\r\n");
#endif
    return FALSE;
  }

  if ((uint16_t)(headerIndex + 2 + (NEC_BITS * 2)) > count) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [ir] NEC decode fail: pulsos insuficientes desde header "
           "(%u)\r\n",
           count - headerIndex);
#endif
    return FALSE;
  }

  for (uint16_t bitIndex = 0; bitIndex < NEC_BITS; bitIndex++) {
    uint16_t i = headerIndex + 2 + (bitIndex * 2);
    uint8_t bit;

    if (i + 1 >= count) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] NEC decode fail: trama truncada en bit %u\r\n",
             bitIndex);
#endif
      return FALSE;
    }

    if (pulses[i].level != markLevel ||
        !irInRange(pulses[i].durationUs, NEC_BIT_MARK_US - NEC_BIT_MARK_TOL_US,
                   NEC_BIT_MARK_US + NEC_BIT_MARK_TOL_US)) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] NEC decode fail: mark invalida bit %u => "
             "(%u,%lu)\r\n",
             bitIndex, pulses[i].level, (unsigned long)pulses[i].durationUs);
#endif
      return FALSE;
    }

    if (pulses[i + 1].level != spaceLevel) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] NEC decode fail: space con nivel invalido bit %u "
             "=> (%u,%lu)\r\n",
             bitIndex, pulses[i + 1].level,
             (unsigned long)pulses[i + 1].durationUs);
#endif
      return FALSE;
    }

    if (irInRange(pulses[i + 1].durationUs,
                  NEC_ZERO_SPACE_US - NEC_BIT_SPACE_TOL_US,
                  NEC_ZERO_SPACE_US + NEC_BIT_SPACE_TOL_US)) {
      bit = 0;
    } else if (irInRange(pulses[i + 1].durationUs,
                         NEC_ONE_SPACE_US - NEC_BIT_SPACE_TOL_US,
                         NEC_ONE_SPACE_US + NEC_BIT_SPACE_TOL_US)) {
      bit = 1;
    } else {
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] NEC decode fail: space fuera de rango bit %u => "
             "%lu us\r\n",
             bitIndex, (unsigned long)pulses[i + 1].durationUs);
#endif
      return FALSE;
    }

    data |= ((uint32_t)bit << bitIndex);
  }

  if ((uint16_t)(headerIndex + 2 + (NEC_BITS * 2)) < count) {
    uint16_t stopIndex = headerIndex + 2 + (NEC_BITS * 2);
    if (stopIndex < count && pulses[stopIndex].level == markLevel &&
        !irInRange(pulses[stopIndex].durationUs,
                   NEC_STOP_MARK_US - NEC_BIT_MARK_TOL_US,
                   NEC_STOP_MARK_US + NEC_BIT_MARK_TOL_US)) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] NEC decode warn: stop mark atipica => %lu us\r\n",
             (unsigned long)pulses[stopIndex].durationUs);
#endif
    }
  }

  {
    uint8_t addr = data & 0xFF;
    uint8_t addrInv = (data >> 8) & 0xFF;
    uint8_t cmd = (data >> 16) & 0xFF;
    uint8_t cmdInv = (data >> 24) & 0xFF;

    if (addr != (uint8_t)~addrInv) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] NEC decode fail: addr checksum invalido (0x%02X "
             "0x%02X)\r\n",
             addr, addrInv);
#endif
      return FALSE;
    }
    if (cmd != (uint8_t)~cmdInv) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [ir] NEC decode fail: cmd checksum invalido (0x%02X "
             "0x%02X)\r\n",
             cmd, cmdInv);
#endif
      return FALSE;
    }

    *address = addr;
    *command = cmd;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [ir] NEC decode OK addr=0x%02X cmd=0x%02X\r\n", *address,
         *command);
#endif

  return TRUE;
}

/// Decodifica NEC usando la última captura.
bool_t irDecodeLastNec(uint8_t *address, uint8_t *command) {
  if (!irCaptureValid) {
    return FALSE;
  }
  return irDecodeNec(irPulseBuffer, irPulseCount, address, command);
}

/// Envía un comando NEC.
void irSendNec(uint8_t address, uint8_t command) {
  uint8_t addressInv = (uint8_t)~address;
  uint8_t commandInv = (uint8_t)~command;
  uint32_t frame = (uint32_t)address | ((uint32_t)addressInv << 8) |
                   ((uint32_t)command << 16) | ((uint32_t)commandInv << 24);

  __disable_irq();

  irSendMark(NEC_HDR_MARK_US);
  irSendSpace(NEC_HDR_SPACE_US);

  for (uint8_t i = 0; i < NEC_BITS; i++) {
    bool_t bit = (frame >> i) & 0x01;
    irSendMark(NEC_BIT_MARK_US);
    irSendSpace(bit ? NEC_ONE_SPACE_US : NEC_ZERO_SPACE_US);
  }

  irSendMark(NEC_STOP_MARK_US);
  irTxOff();

  __enable_irq();

#ifdef MEDIUM_DEBUG
  printf("[modules] [ir] NEC TX addr=0x%02X cmd=0x%02X\r\n", address, command);
#endif
}
