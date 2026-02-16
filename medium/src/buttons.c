/**
 * @file buttons.c
 * @brief Button and joystick input implementation
 *
 * Adapted from branch_ui. Reads two digital buttons and an analog
 * joystick via ADC with deadzone processing.
 */

#include "buttons.h"

void buttonsInit(void)
{
    gpioInit(SW_ENTER_PORT, GPIO_INPUT);
    gpioInit(SW_BACK_PORT, GPIO_INPUT);
}

bool_t swEnterRead(void)
{
    return !gpioRead(SW_ENTER_PORT);
}

bool_t swBackRead(void)
{
    return !gpioRead(SW_BACK_PORT);
}

JoystickState joystickRead(void)
{
    JoystickState state;
    int16_t x = adcRead(JOYSTICK_PORT_X);
    int16_t y = adcRead(JOYSTICK_PORT_Y);

    /* Deadzone: center is 300-900 (10-bit ADC, 0-1023 range) */
    state.x = (x < 300) ? -1 : (x > 900) ? 1 : 0;
    state.y = (y < 300) ? -1 : (y > 900) ? 1 : 0;

    return state;
}
