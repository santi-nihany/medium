//===----------------------------------------------------------------------===//
///
/// \file
/// Captura y reproducción de tramas RF con CC1101.
///
//===----------------------------------------------------------------------===//

#ifndef RF_H
#define RF_H

#include "main.h"
#include "drivers/cc1101.h"

/// Cantidad máxima de duraciones (en us) capturadas.
#define RF_CAPTURE_PULSES_MAX 512

typedef bool_t (*rfCancelCallback_t)(void *context);

bool_t rfInit(void);
bool_t rfSetCaptureConfig(const cc1101OokConfig_t *config);
bool_t rfSetCaptureBand(cc1101Band_t band);
bool_t rfCapture(void);
bool_t rfCaptureWithCancel(rfCancelCallback_t cancelCallback, void *context);
bool_t rfCapture433MHz(void);
bool_t rfCapture433MHzWithCancel(rfCancelCallback_t cancelCallback,
                                 void *context);
bool_t rfCapture315MHz(void);
bool_t rfCapture315MHzWithCancel(rfCancelCallback_t cancelCallback,
                                 void *context);
bool_t rfReplayCaptured(void);
bool_t rfHasCapture(void);
bool_t rfGetLastCapture(const uint16_t **pulsesUs, uint16_t *count,
                        bool_t *firstLevel);
cc1101Band_t rfLastCaptureBand(void);

#endif
