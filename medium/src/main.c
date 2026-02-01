/**
 * @file main.c
 * @brief Main entry point for Médium Device firmware
 * 
 * Proyecto Médium: Captura, almacenamiento y reproducción de señales IR/RF
 * 
 * Arquitectura:
 * - Event-Driven / Time-Driven híbrida sobre RTOS preemptivo
 * - Interrupciones HW capturan señales IR/RF mediante StreamBuffers
 * - Tareas de alta prioridad procesan y almacenan en microSD
 * - UI Task gestiona interfaz de usuario y comandos
 */

/*==================[inclusions]=============================================*/

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "stream_buffer.h"
#include "queue.h"
#include "semphr.h"

#include "sapi.h"

#include "signal_capture.h"
#include "signal_storage.h"
#include "signal_replay.h"
#include "ui_controller.h"
#include "housekeeping.h"
#include "test_storage.h"
#include "mock_signal_generator.h"

/* FatFS includes for disk timer */
#include "ff.h"

/* Forward declaration */
void diskTickHook(void *ptr);
void disk_timerproc(void);

/*==================[macros and definitions]=================================*/

/* Task priorities (higher number = higher priority) */
#define PRIORITY_SIGNAL_CAPTURE_IR     4
#define PRIORITY_SIGNAL_CAPTURE_RF     4
#define PRIORITY_STORAGE_TASK          3
#define PRIORITY_REPLAY_TASK           2
#define PRIORITY_UI_TASK               1
#define PRIORITY_HOUSEKEEPING_TASK     1
#define PRIORITY_TEST_STORAGE_TASK     1
#define PRIORITY_MOCK_GENERATOR        1  /* Low priority - runs in background */

/* Task stack sizes */
#define STACK_SIZE_CAPTURE            512
#define STACK_SIZE_STORAGE           1024
#define STACK_SIZE_REPLAY             512
#define STACK_SIZE_UI                1024
#define STACK_SIZE_HOUSEKEEPING       256
#define STACK_SIZE_TEST_STORAGE       512
#define STACK_SIZE_MOCK_GENERATOR     256

/* StreamBuffer and Queue sizes */
#define STREAM_BUFFER_SIZE           2048
#define STORAGE_QUEUE_SIZE            10
#define UI_COMMAND_QUEUE_SIZE          20

/*==================[internal data definition]===============================*/

/* FreeRTOS handles */
StreamBufferHandle_t xStreamBufferIR = NULL;
StreamBufferHandle_t xStreamBufferRF = NULL;
QueueHandle_t xStorageQueue = NULL;
QueueHandle_t xUICommandQueue = NULL;
SemaphoreHandle_t xStorageMutex = NULL;

/* Task handles */
static TaskHandle_t xTaskSignalCaptureIR = NULL;
static TaskHandle_t xTaskSignalCaptureRF = NULL;
static TaskHandle_t xTaskStorage = NULL;
static TaskHandle_t xTaskReplay = NULL;
static TaskHandle_t xTaskUI = NULL;
static TaskHandle_t xTaskHousekeeping = NULL;
static TaskHandle_t xTaskTestStorage = NULL;
static TaskHandle_t xTaskMockGenerator = NULL;

/*==================[external functions definition]==========================*/

// FUNCION que se ejecuta cada vezque ocurre un Tick
void diskTickHook( void *ptr );


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

    /* SPI configuration for SD card */
    spiConfig(SPI0);
    
    /* Configure tick hook for disk timer (required by FatFS) */
    tickConfig(10);  // 10ms tick resolution
    tickCallbackSet(diskTickHook, NULL);
    
    /* TODO: Initialize IR receiver/transmitter */
    /* TODO: Initialize RF receiver/transmitter */
    /* TODO: Initialize LCD display */
    /* TODO: Initialize GPIO for buttons */
    /* TODO: Initialize Timer Capture for IR/RF */
    /* TODO: Initialize RTC for timestamps */
    
    printf("Hardware initialized.\r\n");
}

/**
 * @brief Creates all FreeRTOS primitives (queues, buffers, semaphores)
 */
static void initRTOSPrimitives(void)
{
    /* Create StreamBuffers for signal capture */
    xStreamBufferIR = xStreamBufferCreate(STREAM_BUFFER_SIZE, sizeof(uint32_t));
    xStreamBufferRF = xStreamBufferCreate(STREAM_BUFFER_SIZE, sizeof(uint32_t));
    
    if (xStreamBufferIR == NULL || xStreamBufferRF == NULL) {
        printf("ERROR: Failed to create StreamBuffers!\r\n");
        while (1);
    }
    
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
    
    /* Create Storage Mutex (only one task can write to SD) */
    xStorageMutex = xSemaphoreCreateMutex();
    if (xStorageMutex == NULL) {
        printf("ERROR: Failed to create Storage Mutex!\r\n");
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

    /* Test Storage Task - disabled for basic RTOS verification
     * Enable when SD card is connected and ready for testing
    xTaskCreate(
        vStorageTest_Task,
        "StorageTest",
        STACK_SIZE_TEST_STORAGE,
        NULL,
        PRIORITY_TEST_STORAGE_TASK,
        &xTaskTestStorage
    );
    */

    /* Mock Signal Generator - for IPC testing without hardware */
    xTaskCreate(
        vMockSignalGenerator_Task,
        "MockGen",
        STACK_SIZE_MOCK_GENERATOR,
        NULL,
        PRIORITY_MOCK_GENERATOR,
        &xTaskMockGenerator
    );

    printf("Tasks created.\r\n");
}

/**
 * @brief Configure hardware interrupts
 */
static void initInterrupts(void)
{
    /* TODO: Configure IR interrupt on edge detection */
    /* TODO: Configure RF interrupt on edge detection */
    /* TODO: Configure button interrupts */
    /* TODO: Configure timer capture interrupts */
    
    /* Enable interrupts */
    __enable_irq();
    
    printf("Interrupts configured.\r\n");
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
    
    /* Configure interrupts */
    initInterrupts();
    
    /* Start the scheduler */
    printf("Starting FreeRTOS scheduler...\r\n");
    vTaskStartScheduler();
    
    /* Should never reach here */
    printf("ERROR: Scheduler exited!\r\n");
    while (1);
    
    return 0;
}

/**
 * @brief Disk timer hook (required by FatFS)
 * Must be called periodically (every 10ms) for SD card timing
 */
void diskTickHook(void *ptr)
{
    disk_timerproc();  // Disk timer process (FatFS internal function)
}

/*==================[end of file]============================================*/

