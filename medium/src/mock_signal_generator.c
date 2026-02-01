/**
 * @file mock_signal_generator.c
 * @brief Mock signal generator for testing IPC without hardware
 *
 * This task simulates what the ISR would do:
 * - Generate samples with delta time and level
 * - Send them to StreamBuffer
 * - Trigger the capture task to process them
 */

#include "mock_signal_generator.h"
#include "signal_capture.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include <stdio.h>

/*==================[external data]==========================================*/

extern StreamBufferHandle_t xStreamBufferIR;

/*==================[external functions]=====================================*/

void vMockSignalGenerator_Task(void *pvParameters)
{
    uint32_t sample;
    uint32_t level = 0;
    uint32_t delta = MOCK_DELTA_TICKS;
    size_t bytes_sent;
    uint32_t burst_count = 0;

    printf("[MockGen] Mock Signal Generator started\r\n");

    for (;;) {
        /* Wait before generating a burst */
        vTaskDelay(pdMS_TO_TICKS(MOCK_BURST_DELAY_MS));

        burst_count++;
        printf("[MockGen] Generating burst #%lu (%d samples)...\r\n",
               (unsigned long)burst_count, MOCK_SAMPLES_PER_BURST);

        /* Generate a burst of samples (simulating a captured signal) */
        for (int i = 0; i < MOCK_SAMPLES_PER_BURST; i++) {
            /* Pack data: {delta_time(24bits), level(8bits)} as uint32_t */
            /* This matches the format used in isr_handlers.c */
            sample = (delta & 0x00FFFFFF) | ((level & 0xFF) << 24);

            /* Send to StreamBuffer (like ISR would do) */
            bytes_sent = xStreamBufferSend(
                xStreamBufferIR,
                &sample,
                sizeof(uint32_t),
                pdMS_TO_TICKS(10)  /* Small timeout */
            );

            if (bytes_sent != sizeof(uint32_t)) {
                printf("[MockGen] WARNING: StreamBuffer full, dropped sample\r\n");
                break;
            }

            /* Toggle level for next sample (simulates alternating edges) */
            level ^= 1;
        }

        printf("[MockGen] Burst #%lu sent to StreamBuffer\r\n", (unsigned long)burst_count);
    }
}
