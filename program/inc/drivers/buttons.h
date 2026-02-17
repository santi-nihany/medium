//===----------------------------------------------------------------------===//
///
/// \file
/// Definiciones para utilzar los botones y un stick analógico (o joystick).
///
//===----------------------------------------------------------------------===//

#ifndef BUTTONS_H
#define BUTTONS_H

#include "main.h"

/// Puerto de SW_ENTER, el botón de acción
#define SW_ENTER_PORT T_COL2
/// Puerto de SW_BACK, el botón de salida
#define SW_BACK_PORT T_FIL1

/// Puerto con el potenciómetro horizontal
#define JOYSTICK_PORT_X CH1
/// Puerto con el potenciómetro vertical
#define JOYSTICK_PORT_Y CH3

/// Estado del joystick
typedef struct {
  /// Valor del eje horizontal (-1 izquierda, 0 centro, 1 derecha)
  int8_t x;
  /// Valor del eje vertical (-1 arriba, 0 centro, 1 abajo)
  int8_t y;
} JoystickState;

void buttonsInit();
bool_t swEnterRead();
bool_t swBackRead();
JoystickState joystickRead();

#endif