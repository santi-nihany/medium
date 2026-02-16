/**
 * @file signal_capture.c
 * @brief Signal capture implementation for IR and RF
 *
 * IR capture: Polling-based via modulo_ir_capture() (same approach as branch_ir)
 * RF capture: Polling-based via CC1101 GDO0 pin (triggered by task notification)
 */

#include "signal_capture.h"
#include "signal_storage.h"
#include "modulo_ir.h"
#include "rf_capture.h"
#include "cc1101.h"
#include <stdio.h>
#include <string.h>

/*==================[internal data]==========================================*/

static volatile BaseType_t xIRCaptureActive = pdFALSE;
static volatile BaseType_t xRFCaptureActive = pdFALSE;
static volatile uint32_t capture_start_time = 0;

/*==================[external data]==========================================*/

extern QueueHandle_t xStorageQueue;
extern SemaphoreHandle_t xSPIMutex;

/*==================[external functions]=====================================*/

void vSignalCaptureIR_Task(void *pvParameters)
{
    IRPulse_t localBuf[MAX_PULSES];
    uint16_t count = 0;
    SignalPacket_t *packet = NULL;

    printf("[CaptureIR] Task started, waiting for notification...\r\n");

    for (;;) {
        /* Wait for UI to enable capture via task notification */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xIRCaptureActive = pdTRUE;
        printf("[CaptureIR] Capture enabled, waiting for IR signal...\r\n");

        /* Polling capture — blocks until frame received or 3s timeout
         * (same approach as branch_ir main loop) */
        bool captured = modulo_ir_capture(localBuf, &count);

        if (captured && count > 0) {
            TickType_t capture_ts = xTaskGetTickCount();

            printf("[CaptureIR] Frame captured: %u pulses\r\n", (unsigned)count);

            /* Try NEC decode (informational) */
            uint8_t addr, cmd;
            if (modulo_ir_decode(localBuf, count, &addr, &cmd)) {
                printf("[CaptureIR] NEC decoded: Addr=0x%02X Cmd=0x%02X\r\n", addr, cmd);
            }

            /* Convert IRPulse_t array → uint32_t samples for SignalPacket_t
             * Format: (duration_us & 0x00FFFFFF) | (level << 24) */
            size_t data_size = count * sizeof(uint32_t);
            packet = pvPortMalloc(sizeof(SignalPacket_t) + data_size);

            if (packet != NULL) {
                packet->mode = SIGNAL_MODE_IR;
                packet->timestamp_ms = capture_ts;
                packet->sample_count = count;

                uint32_t *samples = (uint32_t *)packet->data;
                for (uint16_t i = 0; i < count; i++) {
                    samples[i] = (localBuf[i].duration & 0x00FFFFFF) |
                                 ((uint32_t)localBuf[i].level << 24);
                }

                /* Send to storage queue */
                if (xQueueSend(xStorageQueue, &packet, pdMS_TO_TICKS(1000)) == pdPASS) {
                    printf("[CaptureIR] Packet (%u pulses) sent to queue\r\n",
                           (unsigned)count);
                } else {
                    vPortFree(packet);
                    printf("[CaptureIR] ERROR: Queue full, packet dropped!\r\n");
                }
            } else {
                printf("[CaptureIR] ERROR: Failed to allocate packet!\r\n");
            }
        } else {
            printf("[CaptureIR] Timeout: no IR signal received\r\n");
        }

        xIRCaptureActive = pdFALSE;
    }
}

void vSignalCaptureRF_Task(void *pvParameters)
{
    SignalPacket_t *packet = NULL;
    UBaseType_t originalPriority;

    printf("[CaptureRF] Task started, waiting for notification...\r\n");

    for (;;) {
        /* Block until UI triggers RF capture */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xRFCaptureActive = pdTRUE;
        printf("[CaptureRF] Capture triggered\r\n");

        /* Allocate packet with 4096-byte data area directly */
        size_t data_size = RF_RECORDING_BUFFER_SIZE;
        packet = pvPortMalloc(sizeof(SignalPacket_t) + data_size);

        if (packet == NULL) {
            printf("[CaptureRF] ERROR: Failed to allocate packet!\r\n");
            xRFCaptureActive = pdFALSE;
            continue;
        }

        /* Take SPI mutex to configure CC1101 for RX */
        if (xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(2000)) == pdPASS) {
            cc1101_setModulation(xCurrentRFConfig.modulation_mode);
            cc1101_setFrequency(xCurrentRFConfig.freq_mhz);
            cc1101_setRxBW(xCurrentRFConfig.bandwidth_khz);
            cc1101_setPktFormat(3);  /* Async mode */
            cc1101_setRxMode();
            xSemaphoreGive(xSPIMutex);
        } else {
            printf("[CaptureRF] ERROR: Cannot take SPI mutex for config\r\n");
            vPortFree(packet);
            xRFCaptureActive = pdFALSE;
            continue;
        }

        /* Raise priority for tight polling */
        originalPriority = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);

        printf("[CaptureRF] Waiting for signal on GDO0...\r\n");

        /* Perform raw capture (GDO0 polling, no SPI during capture) */
        bool_t captured = rf_capture_raw(packet->data,
                                          (int)xCurrentRFConfig.delay_us);

        /* Restore priority */
        vTaskPrioritySet(NULL, originalPriority);

        if (captured) {
            packet->mode = SIGNAL_MODE_RF;
            packet->timestamp_ms = xTaskGetTickCount();
            packet->sample_count = RF_RECORDING_BUFFER_SIZE;  /* byte count */

            printf("[CaptureRF] Signal captured (%u bytes)\r\n",
                   (unsigned)RF_RECORDING_BUFFER_SIZE);

            /* Send to storage queue */
            if (xQueueSend(xStorageQueue, &packet, pdMS_TO_TICKS(1000)) == pdPASS) {
                printf("[CaptureRF] Packet sent to storage queue\r\n");
            } else {
                vPortFree(packet);
                printf("[CaptureRF] ERROR: Queue full, packet dropped!\r\n");
            }
        } else {
            printf("[CaptureRF] Timeout: no RF signal detected\r\n");
            vPortFree(packet);
        }

        xRFCaptureActive = pdFALSE;
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
