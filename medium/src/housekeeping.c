/**
 * @file housekeeping.c
 * @brief Housekeeping implementation
 */

#include "housekeeping.h"
#include <stdio.h>

/*==================[internal data]==========================================*/

static uint32_t min_free_heap = UINT32_MAX;

/*==================[external functions]=====================================*/

void vHousekeeping_Task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();

    printf("Housekeeping Task started.\r\n");

    for (;;) {
        /* Monitor free heap */
        uint32_t free_heap = xPortGetFreeHeapSize();
        if (free_heap < min_free_heap) {
            min_free_heap = free_heap;
        }

        printf("[Housekeeping] Free heap: %lu bytes, Min: %lu bytes\r\n",
               (unsigned long)free_heap, (unsigned long)min_free_heap);

        /* TODO: Feed watchdog */
        /* Housekeeping_FeedWatchdog(); */

        /* Maintain periodic delay (3 seconds) */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(3000));
    }
}

uint32_t Housekeeping_GetFreeHeap(void)
{
    return xPortGetFreeHeapSize();
}

uint32_t Housekeeping_GetMinFreeHeap(void)
{
    return min_free_heap;
}

void Housekeeping_FeedWatchdog(void)
{
    /* TODO: Implement watchdog feed */
}

