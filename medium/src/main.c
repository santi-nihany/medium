/**
 * @file main.c
 * @brief Main entry point for Médium Device firmware
 *
 * Proyecto Médium: Captura, almacenamiento y reproducción de señales IR/RF
 *
 * Arquitectura:
 * - Polling-based capture tasks triggered by UI via task notifications
 * - IR capture: modulo_ir_capture() polling at 30us
 * - RF capture: GDO0 polling via rf_capture_raw()
 * - Storage task writes to microSD via FatFS
 * - UI Task gestiona interfaz de usuario y comandos
 */

/*==================[inclusions]=============================================*/

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"

#include "sapi.h"
#include "chip.h"

#include "signal_capture.h"
#include "signal_storage.h"
#include "signal_replay.h"
#include "ui_controller.h"
#include "housekeeping.h"

/* Module includes */
#include "cc1101.h"
#include "rf_capture.h"
#include "modulo_ir.h"
#include "sh1106.h"
#include "buttons.h"

/* FatFS includes for disk timer */
#include "ff.h"

/* Forward declarations */
void disk_timerproc(void);
static void vDiskTimerCallback(TimerHandle_t xTimer);

/*==================[SD card CS pin remapping]===============================*/

/* SD card CS: GPIO3[12] on SCU P7_4 (instead of default GPIO3[0] on P6_1)
 * FSSDC_CS_PORT and FSSDC_CS_PIN are defined in config.mk via DEFINES+= */
#define SD_CS_SCU_PORT   7
#define SD_CS_SCU_PIN    4
#define SD_CS_GPIO_PORT  3
#define SD_CS_GPIO_PIN   12

/*==================[macros and definitions]=================================*/

/* Task priorities (higher number = higher priority) */
#define PRIORITY_SIGNAL_CAPTURE_IR     4
#define PRIORITY_SIGNAL_CAPTURE_RF     4
#define PRIORITY_STORAGE_TASK          3
#define PRIORITY_REPLAY_TASK           2
#define PRIORITY_UI_TASK               1
#define PRIORITY_HOUSEKEEPING_TASK     1

/* Task stack sizes */
#define STACK_SIZE_CAPTURE            512
#define STACK_SIZE_STORAGE           1024
#define STACK_SIZE_REPLAY             512
#define STACK_SIZE_UI                1024
#define STACK_SIZE_HOUSEKEEPING       256

/* Queue sizes */
#define STORAGE_QUEUE_SIZE            10
#define UI_COMMAND_QUEUE_SIZE          20

/*==================[internal data definition]===============================*/

/* FreeRTOS handles */
QueueHandle_t xStorageQueue = NULL;
QueueHandle_t xUICommandQueue = NULL;
SemaphoreHandle_t xSPIMutex = NULL;
TimerHandle_t xDiskTimer = NULL;

/* Task handles — non-static so UI controller can send notifications */
TaskHandle_t xTaskSignalCaptureIR = NULL;
TaskHandle_t xTaskSignalCaptureRF = NULL;
TaskHandle_t xTaskStorage = NULL;
TaskHandle_t xTaskReplay = NULL;
TaskHandle_t xTaskUI = NULL;
static TaskHandle_t xTaskHousekeeping = NULL;

/*==================[internal functions]=====================================*/

/**
 * @brief Initializes all hardware peripherals
 */
static void initHardware(void)
{
    /* Initialize board and basic peripherals */
    boardConfig();

    /* Initialize UART for debugging */
    uartConfig(UART_USB, 115200);
    printf("\r\n=== Médium Device Firmware ===\r\n");

    /* SPI configuration for SD card and CC1101 (shared SPI0 bus) */
    spiConfig(SPI0);

    /* Remap SD card CS to GPIO3[12] (SCU P7_4)
     * Must be done AFTER spiConfig() which sets up the default GPIO3[0].
     * FSSDC uses FSSDC_CS_PORT/FSSDC_CS_PIN macros (overridden in config.mk). */
    Chip_SCU_PinMuxSet(SD_CS_SCU_PORT, SD_CS_SCU_PIN,
                       (SCU_MODE_PULLUP | SCU_MODE_FUNC0));
    Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, SD_CS_GPIO_PORT, SD_CS_GPIO_PIN);
    Chip_GPIO_SetPinOutHigh(LPC_GPIO_PORT, SD_CS_GPIO_PORT, SD_CS_GPIO_PIN);

    /* === CC1101 RF transceiver init ===
     * Must be AFTER spiConfig(SPI0) and SD CS remap.
     * cc1101_initGPIO() reclaims P6_1 (GPIO3[0]) for GDO2 input. */
    cc1101_initGPIO();
    printf("[RF] CC1101 GPIO initialized\r\n");

    /* Detect and configure CC1101 */
    cc1101_init();
    if (cc1101_detect()) {
        uint8_t version = cc1101_readStatus(CC1101_VERSION);
        printf("[RF] CC1101 detected (version=0x%02X)\r\n", version);
        rf_setup();
        printf("[RF] CC1101 configured for async RX\r\n");
    } else {
        printf("[RF] WARNING: CC1101 not detected!\r\n");
    }

    /* === IR module init ===
     * Configures GPIO7 (IR RX) as input, GPIO5 (IR TX) as output,
     * TIMER2 as free-running us counter */
    modulo_ir_init();
    printf("[IR] IR module initialized\r\n");

    /* === ADC + Buttons/Joystick init ===
     * ADC for joystick analog axes, GPIO for ENTER/BACK buttons */
    adcConfig(ADC_ENABLE);
    buttonsInit();
    printf("[UI] Buttons and joystick initialized\r\n");

    /* === SH1106 OLED display init ===
     * I2C0 at 100kHz, then SH1106 init sequence */
    i2cInit(I2C0, 100000);
    sh1106_init();
    printf("[UI] SH1106 OLED display initialized\r\n");

    printf("Hardware initialized.\r\n");
}

/**
 * @brief Creates all FreeRTOS primitives (queues, semaphores, timers)
 */
static void initRTOSPrimitives(void)
{
    /* Create Storage Queue */
    xStorageQueue = xQueueCreate(STORAGE_QUEUE_SIZE, sizeof(SignalPacket_t*));
    if (xStorageQueue == NULL) {
        printf("ERROR: Failed to create Storage Queue!\r\n");
        while (1);
    }

    /* Create UI Command Queue */
    xUICommandQueue = xQueueCreate(UI_COMMAND_QUEUE_SIZE, sizeof(UICommand_t));
    if (xUICommandQueue == NULL) {
        printf("ERROR: Failed to create UI Command Queue!\r\n");
        while (1);
    }

    /* Create SPI Mutex (serializes ALL SPI0 access: SD + CC1101) */
    xSPIMutex = xSemaphoreCreateMutex();
    if (xSPIMutex == NULL) {
        printf("ERROR: Failed to create SPI Mutex!\r\n");
        while (1);
    }

    /* Create Disk Timer for FatFS (10ms period, auto-reload) */
    xDiskTimer = xTimerCreate(
        "DiskTimer",
        pdMS_TO_TICKS(10),      /* 10ms period */
        pdTRUE,                  /* Auto-reload */
        NULL,                    /* Timer ID (unused) */
        vDiskTimerCallback       /* Callback function */
    );
    if (xDiskTimer == NULL) {
        printf("ERROR: Failed to create Disk Timer!\r\n");
        while (1);
    }

    /* Start the disk timer */
    if (xTimerStart(xDiskTimer, 0) != pdPASS) {
        printf("ERROR: Failed to start Disk Timer!\r\n");
        while (1);
    }

    printf("RTOS primitives created.\r\n");
}

/**
 * @brief Creates all FreeRTOS tasks
 */
static void createTasks(void)
{
    /* Create Signal Capture Tasks */
    xTaskCreate(
        vSignalCaptureIR_Task,
        "SignalCaptureIR",
        STACK_SIZE_CAPTURE,
        NULL,
        PRIORITY_SIGNAL_CAPTURE_IR,
        &xTaskSignalCaptureIR
    );

    xTaskCreate(
        vSignalCaptureRF_Task,
        "SignalCaptureRF",
        STACK_SIZE_CAPTURE,
        NULL,
        PRIORITY_SIGNAL_CAPTURE_RF,
        &xTaskSignalCaptureRF
    );

    /* Create Storage Task */
    xTaskCreate(
        vStorage_Task,
        "Storage",
        STACK_SIZE_STORAGE,
        NULL,
        PRIORITY_STORAGE_TASK,
        &xTaskStorage
    );

    /* Create Replay Task */
    xTaskCreate(
        vReplay_Task,
        "Replay",
        STACK_SIZE_REPLAY,
        NULL,
        PRIORITY_REPLAY_TASK,
        &xTaskReplay
    );

    /* Create UI Task */
    xTaskCreate(
        vUI_Task,
        "UI",
        STACK_SIZE_UI,
        NULL,
        PRIORITY_UI_TASK,
        &xTaskUI
    );

    /* Create Housekeeping Task */
    xTaskCreate(
        vHousekeeping_Task,
        "Housekeeping",
        STACK_SIZE_HOUSEKEEPING,
        NULL,
        PRIORITY_HOUSEKEEPING_TASK,
        &xTaskHousekeeping
    );

    printf("Tasks created.\r\n");
}

/**
 * @brief Main entry point
 */
int main(void)
{
    /* Initialize hardware */
    initHardware();

    /* Initialize RTOS primitives */
    initRTOSPrimitives();

    /* Create tasks */
    createTasks();

    /* Start the scheduler */
    printf("Starting FreeRTOS scheduler...\r\n");
    vTaskStartScheduler();

    /* Should never reach here */
    printf("ERROR: Scheduler exited!\r\n");
    while (1);

    return 0;
}

/**
 * @brief Disk timer callback (FreeRTOS software timer)
 * Called every 10ms to drive FatFS timing for SD card operations
 */
static void vDiskTimerCallback(TimerHandle_t xTimer)
{
    (void)xTimer;
    disk_timerproc();  // FatFS internal timing function
}

/*==================[end of file]============================================*/
