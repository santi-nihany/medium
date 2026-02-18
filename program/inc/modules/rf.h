//===----------------------------------------------------------------------===//
///
/// \file
/// Captura y reproducción de tramas RF con CC1101.
///
//===----------------------------------------------------------------------===//

#ifndef RF_H
#define RF_H

#include "main.h"

/// Cantidad máxima de duraciones (en us) capturadas.
#define RF_CAPTURE_PULSES_MAX 512

bool_t rfInit(void);
bool_t rfCapture315MHz(void);
bool_t rfReplayCaptured(void);
bool_t rfHasCapture(void);

#endif
