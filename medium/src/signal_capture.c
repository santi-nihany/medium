/**
 * @file signal_capture.c
 * @brief Signal capture implementation
 */

#include "signal_capture.h"
#include "signal_storage.h"
#include <stdio.h>
#include <string.h>

/*==================[internal data]==========================================*/

static volatile BaseType_t xIRCaptureActive = pdFALSE;
static volatile BaseType_t xRFCaptureActive = pdFALSE;
static volatile uint32_t capture_start_time = 0;

/*==================[external data]==========================================*/

extern StreamBufferHandle_t xStreamBufferIR;
extern StreamBufferHandle_t xStreamBufferRF;
extern QueueHandle_t xStorageQueue;
extern SemaphoreHandle_t xStorageMutex;

/*==================[external functions]=====================================*/

void vSignalCaptureIR_Task(void *pvParameters)
{
    uint32_t sample_buffer[256];
    SignalPacket_t *packet = NULL;
    size_t bytes_received;

    printf("[CaptureIR] Task started, waiting for data...\r\n");

    for (;;) {
        /* Block waiting for data from StreamBuffer (from ISR or mock generator) */
        bytes_received = xStreamBufferReceive(
            xStreamBufferIR,
            sample_buffer,
            sizeof(sample_buffer),
            portMAX_DELAY
        );

        if (bytes_received > 0) {
            uint32_t sample_count = bytes_received / sizeof(uint32_t);
            printf("[CaptureIR] Received %lu bytes (%lu samples)\r\n",
                   (unsigned long)bytes_received, (unsigned long)sample_count);

            /* Allocate packet with room for data */
            packet = pvPortMalloc(sizeof(SignalPacket_t) + bytes_received);
            if (packet != NULL) {
                packet->mode = SIGNAL_MODE_IR;
                packet->timestamp_ms = xTaskGetTickCount();
                packet->sample_count = sample_count;
                memcpy(packet->data, sample_buffer, bytes_received);

                /* Send pointer to storage queue */
                if (xStorageQueue != NULL) {
                    if (xQueueSend(xStorageQueue, &packet, pdMS_TO_TICKS(100)) == pdPASS) {
                        printf("[CaptureIR] Packet sent to storage queue\r\n");
                    } else {
                        vPortFree(packet);
                        printf("[CaptureIR] ERROR: Storage queue full!\r\n");
                    }
                }
            } else {
                printf("[CaptureIR] ERROR: Failed to allocate packet!\r\n");
            }
        }
    }
}

void vSignalCaptureRF_Task(void *pvParameters)
{
    uint32_t sample_buffer[256];
    SignalPacket_t *packet = NULL;
    size_t bytes_received;

    (void)sample_buffer;  /* Suppress unused warning - RF not used in mock test */
    (void)packet;
    (void)bytes_received;

    printf("[CaptureRF] Task started (idle for mock test)\r\n");

    for (;;) {
        /* RF task stays idle during mock testing - only IR is tested */
        vTaskDelay(pdMS_TO_TICKS(10000));
        // /* Wait for watermark or notification that capture is complete */
        // bytes_received = xStreamBufferReceive(
        //     xStreamBufferRF,
        //     sample_buffer,
        //     sizeof(sample_buffer),
        //     portMAX_DELAY
        // );
        
        // if (bytes_received > 0 && xRFCaptureActive) {
        //     /* Create packet with captured data */
        //     packet = pvPortMalloc(sizeof(SignalPacket_t) + bytes_received);
        //     if (packet != NULL) {
        //         packet->mode = SIGNAL_MODE_RF;
        //         packet->timestamp_ms = capture_start_time;
        //         packet->sample_count = bytes_received / sizeof(uint32_t);
        //         memcpy(packet->data, sample_buffer, bytes_received);
                
        //         /* Send to storage queue */
        //         if (xStorageQueue != NULL) {
        //             if (xQueueSend(xStorageQueue, &packet, 0) != pdPASS) {
        //                 /* Queue full, free packet */
        //                 vPortFree(packet);
        //                 printf("ERROR: Storage queue full!\r\n");
        //             }
        //         }
        //     }
        // }
        
        // /* Small delay to prevent CPU spinning */
        // vTaskDelay(1);
    }
}

void SignalCapture_Start(uint8_t mode)
{
    if (mode == SIGNAL_MODE_IR) {
        xIRCaptureActive = pdTRUE;
        capture_start_time = xTaskGetTickCount();
        printf("IR capture started.\r\n");
    } else if (mode == SIGNAL_MODE_RF) {
        xRFCaptureActive = pdTRUE;
        capture_start_time = xTaskGetTickCount();
        printf("RF capture started.\r\n");
    }
}

void SignalCapture_Stop(uint8_t mode)
{
    if (mode == SIGNAL_MODE_IR) {
        xIRCaptureActive = pdFALSE;
        printf("IR capture stopped.\r\n");
    } else if (mode == SIGNAL_MODE_RF) {
        xRFCaptureActive = pdFALSE;
        printf("RF capture stopped.\r\n");
    }
}

BaseType_t SignalCapture_IsActive(uint8_t mode)
{
    if (mode == SIGNAL_MODE_IR) {
        return xIRCaptureActive;
    } else if (mode == SIGNAL_MODE_RF) {
        return xRFCaptureActive;
    }
    return pdFALSE;
}

