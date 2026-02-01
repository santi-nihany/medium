/**
 * @file signal_replay.c
 * @brief Signal replay implementation
 */

#include "signal_replay.h"
#include "signal_storage.h"
#include <stdio.h>
#include <string.h>

/*==================[internal data]==========================================*/

static ReplayState_t xReplayState = REPLAY_STATE_IDLE;
static uint8_t xReplayProgress = 0;
static char replay_filename[64] = {0};

/*==================[external functions]=====================================*/

void vReplay_Task(void *pvParameters)
{
    printf("Replay Task started.\r\n");

    for (;;) {
        printf("hello from signal replay\r\n");
        vTaskDelay(pdMS_TO_TICKS(4000));

        /* TODO: Enable state machine when ready for replay testing
        switch (xReplayState) {
            case REPLAY_STATE_IDLE:
                vTaskDelay(pdMS_TO_TICKS(100));
                break;

            case REPLAY_STATE_LOADING:
                printf("Loading signal: %s\r\n", replay_filename);
                xReplayState = REPLAY_STATE_READY;
                break;

            case REPLAY_STATE_READY:
                vTaskDelay(pdMS_TO_TICKS(50));
                break;

            case REPLAY_STATE_PLAYING:
                printf("Replaying...\r\n");
                vTaskDelay(pdMS_TO_TICKS(100));
                xReplayProgress++;
                if (xReplayProgress >= 100) {
                    xReplayProgress = 0;
                    xReplayState = REPLAY_STATE_IDLE;
                }
                break;

            case REPLAY_STATE_ERROR:
                vTaskDelay(pdMS_TO_TICKS(1000));
                xReplayState = REPLAY_STATE_IDLE;
                break;
        }
        */
    }
}

BaseType_t Replay_Start(const char *filename)
{
    if (xReplayState != REPLAY_STATE_IDLE) {
        return pdFAIL;
    }
    
    strncpy(replay_filename, filename, sizeof(replay_filename) - 1);
    xReplayProgress = 0;
    xReplayState = REPLAY_STATE_LOADING;
    
    printf("Starting replay: %s\r\n", filename);
    
    return pdPASS;
}

void Replay_Stop(void)
{
    xReplayState = REPLAY_STATE_IDLE;
    xReplayProgress = 0;
    printf("Replay stopped.\r\n");
}

ReplayState_t Replay_GetState(void)
{
    return xReplayState;
}

uint8_t Replay_GetProgress(void)
{
    return xReplayProgress;
}

