/**
 * @file test_storage.c
 * @brief Periodic test task that enqueues synthetic SignalPacket_t to storage
 */

#include "FreeRTOS.h"
#include "task.h"
#include "sapi.h"

#include "signal_capture.h"   // For SIGNAL_MODE_IR/RF
#include "signal_storage.h"   // For Storage_SaveSignal

#define TEST_SAMPLE_COUNT          128     /* number of uint32_t samples */
#define TEST_RETRY_DELAY_MS        200

static void fillSyntheticSamples(uint32_t *dst, uint32_t count)
{
    /* Generate a simple square wave timing pattern in ticks
     * Pack format expected by storage: lower 24 bits = delta, upper 8 bits = level */
    uint32_t level = 0;
    uint32_t delta = 500; /* arbitrary delta ticks */

    for (uint32_t i = 0; i < count; i++) {
        uint32_t sample = (delta & 0x00FFFFFFu) | ((level & 0xFFu) << 24);
        dst[i] = sample;
        level ^= 1u; /* toggle level */
    }
}

void vStorageTest_Task(void *pvParameters)
{
    (void) pvParameters;

    printf("[Test] Storage single-shot test task started\r\n");

    /* Allocate packet with room for TEST_SAMPLE_COUNT uint32_t samples */
    const uint32_t dataSize = TEST_SAMPLE_COUNT * sizeof(uint32_t);
    SignalPacket_t *packet = (SignalPacket_t *) pvPortMalloc(sizeof(SignalPacket_t) + dataSize);
    if (packet == NULL) {
        printf("[Test] ERROR: malloc packet failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    packet->mode = SIGNAL_MODE_IR; /* test with IR mode */
    packet->timestamp_ms = (uint32_t) xTaskGetTickCount();
    packet->sample_count = TEST_SAMPLE_COUNT;

    /* Fill synthetic data */
    fillSyntheticSamples((uint32_t *)packet->data, TEST_SAMPLE_COUNT);

    /* Retry until the packet is successfully enqueued (ownership transfers to storage) */
    for (;;) {
        if (Storage_SaveSignal(packet) == pdPASS) {
            printf("[Test] Enqueued synthetic signal: %lu samples\r\n", (unsigned long)packet->sample_count);
            break;
        }
        printf("[Test] WARN: enqueue failed, retrying...\r\n");
        vTaskDelay(pdMS_TO_TICKS(TEST_RETRY_DELAY_MS));
    }

    /* Done: this task can terminate now */
    vTaskDelete(NULL);
}
