/**
 * @file ui_controller.c
 * @brief UI Controller implementation
 * 
 * Implements the UI state machine according to the flowchart:
 * - Menú
 * - Capturar señal IR
 * - Capturar señal RF
 * - Finalizado (Guardar/Descartar)
 * - Archivos (Reproducir, Borrar archivo)
 */

#include "ui_controller.h"
#include "signal_capture.h"
#include "signal_replay.h"
#include "signal_storage.h"
#include <stdio.h>

/*==================[internal data]==========================================*/

static UIState_t xUIState = UI_STATE_MENU;
static uint8_t current_selection = 0;
static char display_buffer[64];

/*==================[external data]==========================================*/

extern QueueHandle_t xUICommandQueue;

/*==================[internal functions]=====================================*/

/**
 * @brief Display main menu
 */
static void DisplayMenu(void)
{
    const char *items[] = {
        "1. Capturar IR",
        "2. Capturar RF",
        "3. Archivos"
    };
    
    printf("\r\n=== MENU ===\r\n");
    for (int i = 0; i < 3; i++) {
        printf("%s%s%s\r\n", 
               (i == current_selection) ? ">" : " ", 
               items[i],
               (i == current_selection) ? " <" : "");
    }
}

/**
 * @brief Display capture screen
 */
static void DisplayCaptureIR(void)
{
    printf("\r\n=== CAPTURAR IR ===\r\n");
    printf("1. Iniciar captura\r\n");
    printf("2. Parar captura\r\n");
    printf("Estado: %s\r\n", 
           SignalCapture_IsActive(SIGNAL_MODE_IR) ? "CAPTURANDO" : "LISTO");
}

/**
 * @brief Display capture screen
 */
static void DisplayCaptureRF(void)
{
    printf("\r\n=== CAPTURAR RF ===\r\n");
    printf("1. Seleccionar frecuencia\r\n");
    printf("2. Iniciar captura\r\n");
    printf("3. Parar captura\r\n");
    printf("Estado: %s\r\n", 
           SignalCapture_IsActive(SIGNAL_MODE_RF) ? "CAPTURANDO" : "LISTO");
}

/**
 * @brief Display finished screen (Guardar/Descartar)
 */
static void DisplayFinished(void)
{
    printf("\r\n=== CAPTURA FINALIZADA ===\r\n");
    printf("1. Guardar\r\n");
    printf("2. Descartar\r\n");
}

/**
 * @brief Display files screen
 */
static void DisplayFiles(void)
{
    printf("\r\n=== ARCHIVOS ===\r\n");
    printf("1. Reproducir\r\n");
    printf("2. Borrar archivo\r\n");
}

/**
 * @brief Process UI command
 */
static void ProcessUICommand(UICommand_t command)
{
    switch (command.event) {
        case UI_EVENT_ACCEPT:
            if (xUIState == UI_STATE_MENU) {
                if (current_selection == 0) {
                    xUIState = UI_STATE_CAPTURE_IR;
                } else if (current_selection == 1) {
                    xUIState = UI_STATE_CAPTURE_RF;
                } else if (current_selection == 2) {
                    xUIState = UI_STATE_FILES;
                }
            }
            break;
            
        case UI_EVENT_BACK:
            if (xUIState != UI_STATE_MENU) {
                xUIState = UI_STATE_MENU;
            }
            break;
            
        case UI_EVENT_START_CAPTURE:
            if (xUIState == UI_STATE_CAPTURE_IR) {
                SignalCapture_Start(SIGNAL_MODE_IR);
            } else if (xUIState == UI_STATE_CAPTURE_RF) {
                SignalCapture_Start(SIGNAL_MODE_RF);
            }
            break;
            
        case UI_EVENT_STOP_CAPTURE:
            if (xUIState == UI_STATE_CAPTURE_IR) {
                SignalCapture_Stop(SIGNAL_MODE_IR);
                xUIState = UI_STATE_FINISHED;
            } else if (xUIState == UI_STATE_CAPTURE_RF) {
                SignalCapture_Stop(SIGNAL_MODE_RF);
                xUIState = UI_STATE_FINISHED;
            }
            break;
            
        case UI_EVENT_SAVE:
            /* TODO: Implement save */
            printf("Guardando...\r\n");
            xUIState = UI_STATE_MENU;
            break;
            
        case UI_EVENT_DISCARD:
            printf("Descartado.\r\n");
            xUIState = UI_STATE_MENU;
            break;
            
        default:
            break;
    }
}

/*==================[external functions]=====================================*/

void vUI_Task(void *pvParameters)
{
    UICommand_t command;
    TickType_t last_wake_time = xTaskGetTickCount();

    (void)command;       /* Suppress unused warning for now */
    (void)last_wake_time;

    printf("UI Task started.\r\n");

    for (;;) {
        printf("hello from ui controller\r\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
        // /* Check for incoming commands */
        // if (xQueueReceive(xUICommandQueue, &command, 0) == pdPASS) {
        //     ProcessUICommand(command);
        // }
        
        // /* Update display based on current state */
        // switch (xUIState) {
        //     case UI_STATE_MENU:
        //         DisplayMenu();
        //         break;
                
        //     case UI_STATE_CAPTURE_IR:
        //         DisplayCaptureIR();
        //         break;
                
        //     case UI_STATE_CAPTURE_RF:
        //         DisplayCaptureRF();
        //         break;
                
        //     case UI_STATE_FINISHED:
        //         DisplayFinished();
        //         break;
                
        //     case UI_STATE_FILES:
        //         DisplayFiles();
        //         break;
                
        //     default:
        //         break;
        // }
        
        // /* Maintain periodic delay */
        // vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(500));
    }
}

void UI_Init(void)
{
    xUIState = UI_STATE_MENU;
    current_selection = 0;
}

void UI_SendCommand(UICommand_t command)
{
    if (xUICommandQueue != NULL) {
        xQueueSend(xUICommandQueue, &command, portMAX_DELAY);
    }
}

UIState_t UI_GetState(void)
{
    return xUIState;
}

void UI_UpdateDisplay(void)
{
    /* Display update is handled by the task */
}

