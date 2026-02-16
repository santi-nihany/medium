//===----------------------------------------------------------------------===//
///
/// \file
/// Inicio del programa. Contiene llamados a inicialización y bucle principal.
///
//===----------------------------------------------------------------------===//

#include "main.h"
#include "buttons.h"
#include "display.h"
#include "sprites.h"

int main(void) {
  // Inicializar placa, puertos y protocolos
  boardConfig();
  adcConfig(ADC_ENABLE);
  i2cConfig(I2C0, 100000);

  // Inicializar componentes
  buttonsInit();
  displayInit();

  bool_t changed = true;
  int8_t selected = -1;
  int8_t last_joystick = 0;

  while (1) {
    JoystickState joystick = joystickRead();
    if (joystick.x != last_joystick) {
      if (joystick.x != 0) {
        changed = true;
        selected = joystick.x;
      }
      last_joystick = joystick.x;
    }

    if (changed) {
      changed = false;
      displayPlace(sprite_background, 0, 0);
      displayPlace(sprite_title, 36, 4);
      displayPlace(sprite_ir, 26, 22);
      displayPlace(sprite_rf, 71, 22);
      if (selected == -1) {
        displayPlace(sprite_lselector, 19, 32);
        displayPlace(sprite_rselector, 59, 32);
      } else if (selected == 1) {
        displayPlace(sprite_lselector, 64, 32);
        displayPlace(sprite_rselector, 104, 32);
      }
      displayUpdate();
    }

    delay(100);
  }

  return 0;
}