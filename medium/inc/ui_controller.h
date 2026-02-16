/**
 * @file ui_controller.h
 * @brief UI Controller module for user interface management
 * 
 * This module manages the user interface state machine, handles button
 * events, and controls LCD display according to the flowchart.
 */

#ifndef _UI_CONTROLLER_H_
#define _UI_CONTROLLER_H_

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"

/*==================[macros and definitions]=================================*/

/* UI States matching the flowchart */
typedef enum {
    UI_STATE_MENU,              /* Main menu */
    UI_STATE_CAPTURE_IR,         /* Capture IR signal */
    UI_STATE_CAPTURE_RF,         /* Capture RF signal */
    UI_STATE_FINISHED,           /* Capture finished (Guardar/Descartar) */
    UI_STATE_FILES,              /* Files menu */
    UI_STATE_REPRODUCE,          /* Reproducir */
    UI_STATE_DELETE_FILE,        /* Borrar archivo */
    UI_STATE_ERROR               /* Error state */
} UIState_t;

/* UI Events */
typedef enum {
    UI_EVENT_NONE,
    UI_EVENT_UP,
    UI_EVENT_DOWN,
    UI_EVENT_ACCEPT,         /* Aceptar/Enter */
    UI_EVENT_BACK,           /* Atrás */
    UI_EVENT_START_CAPTURE,
    UI_EVENT_STOP_CAPTURE,
    UI_EVENT_SAVE,
    UI_EVENT_DISCARD,
    UI_EVENT_CAPTURE_SUCCESS,
    UI_EVENT_CAPTURE_ERROR,
    UI_EVENT_START_REPLAY,
    UI_EVENT_STOP_REPLAY,
    UI_EVENT_DELETE_CONFIRM
} UIEvent_t;

/* UI Commands */
typedef struct {
    UIEvent_t event;
    uint32_t param;  /* Event parameter */
} UICommand_t;

/*==================[external functions]=====================================*/

/**
 * @brief UI Task
 * Manages the user interface state machine and display
 * @param pvParameters Task parameters (unused)
 */
void vUI_Task(void *pvParameters);

/**
 * @brief Initialize UI module
 */
void UI_Init(void);

/**
 * @brief Send UI command
 * @param command Command to send
 */
void UI_SendCommand(UICommand_t command);

/**
 * @brief Get current UI state
 * @return Current UI state
 */
UIState_t UI_GetState(void);

/**
 * @brief Update display
 * Should be called periodically to refresh LCD
 */
void UI_UpdateDisplay(void);

/*==================[end of file]============================================*/

#endif /* _UI_CONTROLLER_H_ */

