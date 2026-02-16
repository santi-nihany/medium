/**
 * @file ui_controller.c
 * @brief UI Controller implementation with SH1106 OLED display
 *
 * Implements the UI state machine:
 * - Menu (Capturar IR / Capturar RF / Archivos)
 * - Capturar señal IR/RF (waiting/capturing/done)
 * - Finalizado (Guardar/Descartar)
 * - Archivos (file list, Reproducir, Borrar)
 *
 * Buttons: TEC1=UP, TEC2=DOWN, TEC3=ACCEPT, TEC4=BACK
 * Display: SH1106 128x64 OLED via I2C0
 */

#include "ui_controller.h"
#include "signal_capture.h"
#include "signal_replay.h"
#include "signal_storage.h"
#include "sh1106.h"
#include "sprites.h"
#include "main.h"
#include "sapi.h"
#include <stdio.h>
#include <string.h>

/*==================[macros]=================================================*/

/* Display layout constants (5x7 font, 6px char width, 8px line height) */
#define CHAR_W          6
#define LINE_H         10   /* 8px font + 2px spacing */
#define HEADER_Y        0
#define CONTENT_Y      14   /* Below header + separator line */
#define MAX_MENU_ITEMS  3
#define MAX_VISIBLE_FILES 4 /* Files visible on screen at once */

/* Button debounce */
#define DEBOUNCE_MS    50
#define UI_POLL_MS     50   /* Main loop poll interval */

/* Splash duration */
#define SPLASH_MS    2000

/*==================[internal data]==========================================*/

static UIState_t xUIState = UI_STATE_MENU;
static uint8_t cursor = 0;          /* Menu/file list cursor position */
static bool_t dirty = TRUE;         /* Screen needs redraw */

/* File list state (limit to 20 entries to save BSS — ~820B vs ~4.1KB) */
#define UI_MAX_FILES    20
static SignalFileInfo_t file_list[UI_MAX_FILES];
static uint32_t file_count = 0;
static uint8_t file_scroll = 0;     /* Top visible file index */

/* Button previous states (for edge detection) */
static bool_t prev_tec1 = TRUE;     /* TRUE = released (active low) */
static bool_t prev_tec2 = TRUE;
static bool_t prev_tec3 = TRUE;
static bool_t prev_tec4 = TRUE;

/* Capture mode tracking (which mode was started) */
static uint8_t capture_mode = SIGNAL_MODE_IR;

/*==================[external data]==========================================*/

extern QueueHandle_t xUICommandQueue;

/*==================[internal functions]=====================================*/

/**
 * @brief Draw header bar with title and separator line
 */
static void DrawHeader(const char *title)
{
    sh1106_drawString(0, HEADER_Y, title, SH1106_WHITE);
    sh1106_drawLine(0, 11, 127, 11, SH1106_WHITE);
}

/**
 * @brief Draw menu screen with cursor
 */
static void DrawMenu(void)
{
    sh1106_fill(SH1106_BLACK);
    DrawHeader("MEDIUM");

    const char *items[] = {
        "Capturar IR",
        "Capturar RF",
        "Archivos"
    };

    for (uint8_t i = 0; i < MAX_MENU_ITEMS; i++) {
        uint8_t y = CONTENT_Y + i * LINE_H;
        if (i == cursor) {
            /* Draw selection indicator */
            sh1106_drawRect(0, y, 128, LINE_H, SH1106_WHITE, TRUE);
            sh1106_drawString(CHAR_W, y + 1, items[i], SH1106_BLACK);
        } else {
            sh1106_drawString(CHAR_W, y + 1, items[i], SH1106_WHITE);
        }
    }

    /* Navigation hint at bottom */
    sh1106_drawString(0, 56, "UP/DN  OK  BACK", SH1106_WHITE);
}

/**
 * @brief Draw capture waiting/active screen
 */
static void DrawCapture(void)
{
    sh1106_fill(SH1106_BLACK);

    if (capture_mode == SIGNAL_MODE_IR) {
        DrawHeader("CAPTURA IR");
    } else {
        DrawHeader("CAPTURA RF");
    }

    uint8_t active = SignalCapture_IsActive(capture_mode);

    if (active) {
        sh1106_drawString(16, 28, "Capturando...", SH1106_WHITE);
        /* Simple animation: rotating bar */
        static uint8_t anim = 0;
        const char bars[] = "|/-\\";
        char anim_str[2] = { bars[anim & 3], '\0' };
        sh1106_drawString(110, 28, anim_str, SH1106_WHITE);
        anim++;
    } else {
        sh1106_drawString(6, 24, "Apunte el control", SH1106_WHITE);
        sh1106_drawString(6, 34, "y presione OK", SH1106_WHITE);
    }

    sh1106_drawString(0, 56, "OK=Iniciar  BACK", SH1106_WHITE);
}

/**
 * @brief Draw capture finished screen (Guardar/Descartar)
 */
static void DrawFinished(void)
{
    sh1106_fill(SH1106_BLACK);
    DrawHeader("CAPTURA OK");

    const char *items[] = {
        "Guardar",
        "Descartar"
    };

    for (uint8_t i = 0; i < 2; i++) {
        uint8_t y = CONTENT_Y + i * LINE_H;
        if (i == cursor) {
            sh1106_drawRect(0, y, 128, LINE_H, SH1106_WHITE, TRUE);
            sh1106_drawString(CHAR_W, y + 1, items[i], SH1106_BLACK);
        } else {
            sh1106_drawString(CHAR_W, y + 1, items[i], SH1106_WHITE);
        }
    }
}

/**
 * @brief Draw files list screen
 */
static void DrawFiles(void)
{
    sh1106_fill(SH1106_BLACK);

    char header[22];
    snprintf(header, sizeof(header), "ARCHIVOS (%lu)", (unsigned long)file_count);
    DrawHeader(header);

    if (file_count == 0) {
        sh1106_drawString(12, 28, "Sin archivos", SH1106_WHITE);
        sh1106_drawString(0, 56, "BACK", SH1106_WHITE);
        return;
    }

    /* Show visible file slice */
    for (uint8_t i = 0; i < MAX_VISIBLE_FILES && (file_scroll + i) < file_count; i++) {
        uint8_t idx = file_scroll + i;
        uint8_t y = CONTENT_Y + i * LINE_H;

        /* Truncate filename for display (max ~20 chars) */
        char display_name[22];
        strncpy(display_name, file_list[idx].filename, 20);
        display_name[20] = '\0';

        if (idx == cursor) {
            sh1106_drawRect(0, y, 128, LINE_H, SH1106_WHITE, TRUE);
            sh1106_drawString(2, y + 1, display_name, SH1106_BLACK);
        } else {
            sh1106_drawString(2, y + 1, display_name, SH1106_WHITE);
        }
    }

    /* Scroll indicators */
    if (file_scroll > 0) {
        sh1106_drawString(120, CONTENT_Y, "^", SH1106_WHITE);
    }
    if (file_scroll + MAX_VISIBLE_FILES < file_count) {
        sh1106_drawString(120, CONTENT_Y + (MAX_VISIBLE_FILES - 1) * LINE_H,
                          "v", SH1106_WHITE);
    }

    sh1106_drawString(0, 56, "OK=Reprod  BACK", SH1106_WHITE);
}

/**
 * @brief Draw error screen
 */
static void DrawError(const char *msg)
{
    sh1106_fill(SH1106_BLACK);
    DrawHeader("ERROR");
    sh1106_drawString(0, 28, msg, SH1106_WHITE);
    sh1106_drawString(0, 56, "OK=Volver", SH1106_WHITE);
}

/**
 * @brief Poll buttons and return detected event (edge detection with debounce)
 */
static UIEvent_t PollButtons(void)
{
    bool_t tec1 = gpioRead(TEC1);
    bool_t tec2 = gpioRead(TEC2);
    bool_t tec3 = gpioRead(TEC3);
    bool_t tec4 = gpioRead(TEC4);

    UIEvent_t event = UI_EVENT_NONE;

    /* Detect falling edge (button press, active low) */
    if (prev_tec1 && !tec1) event = UI_EVENT_UP;
    if (prev_tec2 && !tec2) event = UI_EVENT_DOWN;
    if (prev_tec3 && !tec3) event = UI_EVENT_ACCEPT;
    if (prev_tec4 && !tec4) event = UI_EVENT_BACK;

    prev_tec1 = tec1;
    prev_tec2 = tec2;
    prev_tec3 = tec3;
    prev_tec4 = tec4;

    return event;
}

/**
 * @brief Handle events in MENU state
 */
static void HandleMenu(UIEvent_t event)
{
    switch (event) {
        case UI_EVENT_UP:
            if (cursor > 0) {
                cursor--;
                dirty = TRUE;
            }
            break;

        case UI_EVENT_DOWN:
            if (cursor < MAX_MENU_ITEMS - 1) {
                cursor++;
                dirty = TRUE;
            }
            break;

        case UI_EVENT_ACCEPT:
            if (cursor == 0) {
                capture_mode = SIGNAL_MODE_IR;
                xUIState = UI_STATE_CAPTURE_IR;
                cursor = 0;
                dirty = TRUE;
            } else if (cursor == 1) {
                capture_mode = SIGNAL_MODE_RF;
                xUIState = UI_STATE_CAPTURE_RF;
                cursor = 0;
                dirty = TRUE;
            } else if (cursor == 2) {
                /* Load file list from SD */
                file_count = Storage_ListFiles(file_list, UI_MAX_FILES);
                xUIState = UI_STATE_FILES;
                cursor = 0;
                file_scroll = 0;
                dirty = TRUE;
            }
            break;

        default:
            break;
    }
}

/**
 * @brief Handle events in CAPTURE_IR or CAPTURE_RF state
 */
static void HandleCapture(UIEvent_t event)
{
    switch (event) {
        case UI_EVENT_ACCEPT:
            if (!SignalCapture_IsActive(capture_mode)) {
                /* Trigger capture via task notification */
                if (capture_mode == SIGNAL_MODE_IR) {
                    xTaskNotifyGive(xTaskSignalCaptureIR);
                    printf("[UI] IR capture triggered\r\n");
                } else {
                    xTaskNotifyGive(xTaskSignalCaptureRF);
                    printf("[UI] RF capture triggered\r\n");
                }
                dirty = TRUE;
            }
            break;

        case UI_EVENT_BACK:
            if (!SignalCapture_IsActive(capture_mode)) {
                xUIState = UI_STATE_MENU;
                cursor = 0;
                dirty = TRUE;
            }
            break;

        default:
            break;
    }

    /* Check if capture just finished → go to FINISHED */
    if (SignalCapture_IsActive(capture_mode) == pdFALSE &&
        (xUIState == UI_STATE_CAPTURE_IR || xUIState == UI_STATE_CAPTURE_RF)) {
        /* Redraw to update capture status */
        dirty = TRUE;
    }
}

/**
 * @brief Handle events in FINISHED state
 */
static void HandleFinished(UIEvent_t event)
{
    switch (event) {
        case UI_EVENT_UP:
        case UI_EVENT_DOWN:
            cursor = (cursor == 0) ? 1 : 0;
            dirty = TRUE;
            break;

        case UI_EVENT_ACCEPT:
            if (cursor == 0) {
                /* Guardar — packet was already queued by capture task */
                printf("[UI] Signal saved\r\n");
            } else {
                /* Descartar */
                printf("[UI] Signal discarded\r\n");
            }
            xUIState = UI_STATE_MENU;
            cursor = 0;
            dirty = TRUE;
            break;

        case UI_EVENT_BACK:
            xUIState = UI_STATE_MENU;
            cursor = 0;
            dirty = TRUE;
            break;

        default:
            break;
    }
}

/**
 * @brief Handle events in FILES state
 */
static void HandleFiles(UIEvent_t event)
{
    switch (event) {
        case UI_EVENT_UP:
            if (cursor > 0) {
                cursor--;
                if (cursor < file_scroll) {
                    file_scroll = cursor;
                }
                dirty = TRUE;
            }
            break;

        case UI_EVENT_DOWN:
            if (cursor < file_count - 1) {
                cursor++;
                if (cursor >= file_scroll + MAX_VISIBLE_FILES) {
                    file_scroll = cursor - MAX_VISIBLE_FILES + 1;
                }
                dirty = TRUE;
            }
            break;

        case UI_EVENT_ACCEPT:
            if (file_count > 0) {
                /* Start replay of selected file */
                printf("[UI] Replay: %s\r\n", file_list[cursor].filename);
                Replay_Start(file_list[cursor].filename);
                xTaskNotifyGive(xTaskReplay);
                xUIState = UI_STATE_REPRODUCE;
                dirty = TRUE;
            }
            break;

        case UI_EVENT_BACK:
            xUIState = UI_STATE_MENU;
            cursor = 0;
            dirty = TRUE;
            break;

        default:
            break;
    }
}

/**
 * @brief Handle events in REPRODUCE state
 */
static void HandleReproduce(UIEvent_t event)
{
    if (event == UI_EVENT_BACK) {
        Replay_Stop();
        xUIState = UI_STATE_FILES;
        cursor = 0;
        file_scroll = 0;
        dirty = TRUE;
    }

    /* Check if replay finished */
    if (Replay_GetState() == REPLAY_STATE_IDLE) {
        xUIState = UI_STATE_FILES;
        dirty = TRUE;
    }
}

/**
 * @brief Handle events in ERROR state
 */
static void HandleError(UIEvent_t event)
{
    if (event == UI_EVENT_ACCEPT || event == UI_EVENT_BACK) {
        xUIState = UI_STATE_MENU;
        cursor = 0;
        dirty = TRUE;
    }
}

/**
 * @brief Process incoming queue commands (from ISRs or other tasks)
 */
static void ProcessQueueCommands(void)
{
    UICommand_t command;
    while (xQueueReceive(xUICommandQueue, &command, 0) == pdPASS) {
        switch (command.event) {
            case UI_EVENT_CAPTURE_SUCCESS:
                if (xUIState == UI_STATE_CAPTURE_IR ||
                    xUIState == UI_STATE_CAPTURE_RF) {
                    xUIState = UI_STATE_FINISHED;
                    cursor = 0;
                    dirty = TRUE;
                }
                break;

            case UI_EVENT_CAPTURE_ERROR:
                xUIState = UI_STATE_ERROR;
                dirty = TRUE;
                break;

            default:
                break;
        }
    }
}

/*==================[external functions]=====================================*/

void vUI_Task(void *pvParameters)
{
    printf("[UI] Task started\r\n");

    /* Show splash screen */
    sh1106_fill(SH1106_BLACK);
    sh1106_place(Sprite_0001, 0, 0);
    sh1106_update();
    vTaskDelay(pdMS_TO_TICKS(SPLASH_MS));

    /* Initialize display with menu */
    dirty = TRUE;

    for (;;) {
        /* Poll buttons */
        UIEvent_t event = PollButtons();

        /* Process commands from queue (other tasks) */
        ProcessQueueCommands();

        /* Handle event based on current state */
        if (event != UI_EVENT_NONE) {
            switch (xUIState) {
                case UI_STATE_MENU:
                    HandleMenu(event);
                    break;

                case UI_STATE_CAPTURE_IR:
                case UI_STATE_CAPTURE_RF:
                    HandleCapture(event);
                    break;

                case UI_STATE_FINISHED:
                    HandleFinished(event);
                    break;

                case UI_STATE_FILES:
                    HandleFiles(event);
                    break;

                case UI_STATE_REPRODUCE:
                    HandleReproduce(event);
                    break;

                case UI_STATE_ERROR:
                    HandleError(event);
                    break;

                default:
                    break;
            }
        }

        /* Redraw screen if needed */
        if (dirty) {
            switch (xUIState) {
                case UI_STATE_MENU:
                    DrawMenu();
                    break;

                case UI_STATE_CAPTURE_IR:
                case UI_STATE_CAPTURE_RF:
                    DrawCapture();
                    break;

                case UI_STATE_FINISHED:
                    DrawFinished();
                    break;

                case UI_STATE_FILES:
                    DrawFiles();
                    break;

                case UI_STATE_REPRODUCE:
                    sh1106_fill(SH1106_BLACK);
                    DrawHeader("REPRODUCIENDO");
                    sh1106_drawString(16, 28, "Enviando...", SH1106_WHITE);
                    sh1106_drawString(0, 56, "BACK=Parar", SH1106_WHITE);
                    break;

                case UI_STATE_ERROR:
                    DrawError("Error inesperado");
                    break;

                default:
                    break;
            }

            sh1106_update();
            dirty = FALSE;
        }

        /* For capture screens, always mark dirty to update animation */
        if ((xUIState == UI_STATE_CAPTURE_IR || xUIState == UI_STATE_CAPTURE_RF) &&
            SignalCapture_IsActive(capture_mode)) {
            dirty = TRUE;
        }

        vTaskDelay(pdMS_TO_TICKS(UI_POLL_MS));
    }
}

void UI_Init(void)
{
    xUIState = UI_STATE_MENU;
    cursor = 0;
    dirty = TRUE;
}

void UI_SendCommand(UICommand_t command)
{
    if (xUICommandQueue != NULL) {
        xQueueSend(xUICommandQueue, &command, pdMS_TO_TICKS(100));
    }
}

UIState_t UI_GetState(void)
{
    return xUIState;
}

void UI_UpdateDisplay(void)
{
    dirty = TRUE;
}

/*==================[end of file]===========================================*/
