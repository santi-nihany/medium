//===----------------------------------------------------------------------===//
///
/// \file
/// Captura y reproducción de tramas RF con CC1101.
///
//===----------------------------------------------------------------------===//

#ifndef RF_H
#define RF_H

#include "drivers/cc1101.h"
#include "main.h"

/// Cantidad máxima de duraciones (en us) capturadas.
#define RF_CAPTURE_PULSES_MAX 512

typedef bool_t (*rfCancelCallback_t)(void *context);

bool_t rfInit(void);
bool_t rfSetCaptureConfig(const cc1101OokConfig_t *config);
bool_t rfCapture(void);
bool_t rfCaptureWithCancel(rfCancelCallback_t cancelCallback, void *context);
bool_t rfRunFrequencyAnalyzer(void);
bool_t rfReplayCaptured(void);
bool_t rfReplayEdges(const uint32_t *edges, uint32_t edgeCount,
                     uint8_t startLevel, int8_t tickScale);
bool_t rfHasCapture(void);
bool_t rfGetLastCapture(const uint16_t **pulsesUs, uint16_t *count,
                        bool_t *firstLevel);
bool_t rfGetActiveConfig(cc1101OokConfig_t *config);
bool_t rfGetLastCaptureConfig(cc1101OokConfig_t *config);

#endif
