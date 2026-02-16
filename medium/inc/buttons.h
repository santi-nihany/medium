/**
 * @file buttons.h
 * @brief Button and joystick input for Médium device
 *
 * Adapted from branch_ui. Two digital buttons (ENTER/BACK) plus an
 * analog joystick (ADC X/Y axes) for menu navigation.
 */

#ifndef _BUTTONS_H_
#define _BUTTONS_H_

#include "sapi.h"

/*==================[pin assignments]=======================================*/

/* Digital buttons */
#define SW_ENTER_PORT   T_COL2
#define SW_BACK_PORT    T_FIL1

/* Analog joystick ADC channels */
#define JOYSTICK_PORT_X CH1
#define JOYSTICK_PORT_Y CH3

/*==================[types]=================================================*/

/**
 * @brief Joystick state after deadzone processing
 */
typedef struct {
    int8_t x;   /* -1 left, 0 center, 1 right */
    int8_t y;   /* -1 up, 0 center, 1 down */
} JoystickState;

/*==================[functions]=============================================*/

/**
 * @brief Initialize button GPIOs (call after boardConfig + adcConfig)
 */
void buttonsInit(void);

/**
 * @brief Read ENTER button (active low, returns TRUE when pressed)
 */
bool_t swEnterRead(void);

/**
 * @brief Read BACK button (active low, returns TRUE when pressed)
 */
bool_t swBackRead(void);

/**
 * @brief Read joystick axes via ADC
 * @return Processed joystick state with deadzone applied
 */
JoystickState joystickRead(void);

#endif /* _BUTTONS_H_ */
