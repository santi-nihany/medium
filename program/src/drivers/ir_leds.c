//===----------------------------------------------------------------------===//
///
/// \file
/// Control de LEDs/sensor IR a nivel de GPIO.
///
//===----------------------------------------------------------------------===//

#include "drivers/ir_leds.h"

/// Inicializa GPIO para TX y RX de infrarrojo.
void irLedsInit(void) {
  gpioConfig(IR_TX_PIN, GPIO_OUTPUT);
  gpioConfig(IR_RX_PIN, GPIO_INPUT);
  irTxOff();
}

/// Activa o desactiva el LED IR de transmisión.
/// \param on verdadero para encender, falso para apagar
void irTxWrite(bool_t on) {
  gpioWrite(IR_TX_PIN, on ? IR_TX_ACTIVE_LEVEL : (1 - IR_TX_ACTIVE_LEVEL));
}

/// Apaga el LED IR de transmisión.
void irTxOff(void) { irTxWrite(FALSE); }

/// Lee el estado lógico del receptor IR.
bool_t irRxRead(void) { return gpioRead(IR_RX_PIN); }
