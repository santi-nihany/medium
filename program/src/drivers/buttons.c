//===----------------------------------------------------------------------===//
///
/// \file
/// Lectura de botones y un stick analógico (o joystick).
///
//===----------------------------------------------------------------------===//

#include "drivers/buttons.h"

// Inicialización de los botones y el stick analógico
// Asume placa inicializada y ADC habilitado
void buttonsInit() {
  // Inicialización de GPIO para los botones
  gpioInit(SW_ENTER_PORT, GPIO_INPUT);
  gpioInit(SW_BACK_PORT, GPIO_INPUT);

#ifdef MEDIUM_DEBUG
  printf("[drivers/buttons] Botones inicializados\r\n");
#endif
}

bool_t swEnterRead() { return !gpioRead(SW_ENTER_PORT); }

bool_t swBackRead() { return !gpioRead(SW_BACK_PORT); }

JoystickState joystickRead() {
  JoystickState state;
  int16_t x = adcRead(JOYSTICK_PORT_X);
  int16_t y = adcRead(JOYSTICK_PORT_Y);

  // Mapear valores de ADC a -1, 0, 1
  state.x = (x < 300) ? -1 : (x > 900) ? 1 : 0;
  state.y = (y < 300) ? -1 : (y > 900) ? 1 : 0;

  return state;
}