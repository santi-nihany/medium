//===----------------------------------------------------------------------===//
///
/// \file
/// Control de LEDs/sensor IR a nivel de GPIO.
///
//===----------------------------------------------------------------------===//

#ifndef IR_LEDS_H
#define IR_LEDS_H

#include "main.h"

/// Pin del emisor IR.
#define IR_TX_PIN GPIO5
/// Pin del receptor IR demodulado.
#define IR_RX_PIN GPIO7
/// Nivel activo del LED IR TX.
#define IR_TX_ACTIVE_LEVEL 1

void irLedsInit(void);
void irTxWrite(bool_t on);
void irTxOff(void);
bool_t irRxRead(void);

#endif
