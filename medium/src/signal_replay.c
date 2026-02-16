/**
 * @file signal_replay.c
 * @brief Signal replay implementation
 *
 * Loads IR signal files from SD via Storage_LoadSignal(), decodes NEC
 * protocol from the stored samples, and replays via modulo_ir_send_nec().
 */

#include "signal_replay.h"
#include "signal_storage.h"
#include "signal_capture.h"
#include "modulo_ir.h"
#include "ui_controller.h"
#include <stdio.h>
#include <string.h>

/*==================[internal data]==========================================*/

static volatile ReplayState_t xReplayState = REPLAY_STATE_IDLE;
static char replay_filename[64] = {0};

/*==================[external data]==========================================*/

extern QueueHandle_t xUICommandQueue;

/*==================[external functions]=====================================*/

void vReplay_Task(void *pvParameters)
{
    SignalPacket_t *packet = NULL;

    printf("[Replay] Task started, waiting for notification...\r\n");

    for (;;) {
        /* Block until UI triggers replay */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (xReplayState != REPLAY_STATE_LOADING) {
            /* Spurious notification — ignore */
            continue;
        }

        printf("[Replay] Loading file: %s\r\n", replay_filename);
        xReplayState = REPLAY_STATE_PLAYING;

        /* Load signal from SD */
        if (Storage_LoadSignal(replay_filename, &packet) != pdPASS) {
            printf("[Replay] ERROR: Failed to load file\r\n");
            xReplayState = REPLAY_STATE_ERROR;

            /* Notify UI of error */
            UICommand_t cmd = { UI_EVENT_CAPTURE_ERROR, 0 };
            xQueueSend(xUICommandQueue, &cmd, pdMS_TO_TICKS(100));

            vTaskDelay(pdMS_TO_TICKS(1000));
            xReplayState = REPLAY_STATE_IDLE;
            continue;
        }

        printf("[Replay] Loaded: mode=%d, samples=%lu\r\n",
               packet->mode, (unsigned long)packet->sample_count);

        if (packet->mode == SIGNAL_MODE_IR) {
            /* Convert packed uint32_t samples back to IRPulse_t for NEC decode
             * Format: bits[23:0] = duration_us, bits[31:24] = level */
            uint32_t *samples = (uint32_t *)packet->data;
            uint32_t count = packet->sample_count;
            IRPulse_t pulses[MAX_PULSES];

            if (count > MAX_PULSES) count = MAX_PULSES;

            for (uint32_t i = 0; i < count; i++) {
                pulses[i].duration = samples[i] & 0x00FFFFFF;
                pulses[i].level = (samples[i] >> 24) & 0xFF;
            }

            /* Try NEC decode */
            uint8_t addr, cmd_nec;
            if (modulo_ir_decode(pulses, (uint16_t)count, &addr, &cmd_nec)) {
                printf("[Replay] NEC decoded: Addr=0x%02X Cmd=0x%02X\r\n",
                       addr, cmd_nec);
                printf("[Replay] Sending NEC...\r\n");

                /* Send NEC command (blocking, ~110ms) */
                modulo_ir_send_nec(addr, cmd_nec);

                printf("[Replay] NEC sent\r\n");
            } else {
                printf("[Replay] WARNING: Not a valid NEC signal, cannot replay\r\n");
            }
        } else {
            printf("[Replay] WARNING: RF replay not implemented\r\n");
        }

        /* Free loaded packet */
        vPortFree(packet);
        packet = NULL;

        xReplayState = REPLAY_STATE_IDLE;
        printf("[Replay] Done\r\n");
    }
}

BaseType_t Replay_Start(const char *filename)
{
    if (xReplayState != REPLAY_STATE_IDLE) {
        return pdFAIL;
    }

    strncpy(replay_filename, filename, sizeof(replay_filename) - 1);
    replay_filename[sizeof(replay_filename) - 1] = '\0';
    xReplayState = REPLAY_STATE_LOADING;

    printf("[Replay] Queued: %s\r\n", filename);

    return pdPASS;
}

void Replay_Stop(void)
{
    xReplayState = REPLAY_STATE_IDLE;
    printf("[Replay] Stopped\r\n");
}

ReplayState_t Replay_GetState(void)
{
    return xReplayState;
}

uint8_t Replay_GetProgress(void)
{
    /* For NEC replay, it's either 0 (not started/done) or 100 (playing) */
    if (xReplayState == REPLAY_STATE_PLAYING) {
        return 50;
    }
    return 0;
}
