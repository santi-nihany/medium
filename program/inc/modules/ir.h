//===----------------------------------------------------------------------===//
///
/// \file
/// Captura, reproducción y codificación NEC para IR.
///
//===----------------------------------------------------------------------===//

#ifndef IR_H
#define IR_H

#include "main.h"

/// Máxima cantidad de pulsos almacenados por captura.
#define IR_MAX_PULSES 200

/// Pulso IR (nivel + duración).
typedef struct {
  uint8_t level;
  uint32_t durationUs;
} IRPulse;

typedef bool_t (*irCancelCallback_t)(void *context);

void irInit(void);
bool_t irRecord(void);
bool_t irRecordWithCancel(irCancelCallback_t cancelCallback, void *context);
bool_t irRecordNoStartTimeoutWithCancel(irCancelCallback_t cancelCallback,
                                        void *context);
bool_t irReplay(void);
bool_t irReplayEdges(const uint32_t *edges, uint32_t edgeCount,
                     uint8_t startLevel, int8_t tickScale);
bool_t irGetLastCapture(const IRPulse **pulses, uint16_t *count);
bool_t irDecodeNec(const IRPulse *pulses, uint16_t count, uint8_t *address,
                   uint8_t *command);
bool_t irDecodeLastNec(uint8_t *address, uint8_t *command);
void irSendNec(uint8_t address, uint8_t command);

#endif
