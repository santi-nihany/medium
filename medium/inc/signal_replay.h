/**
 * @file signal_replay.h
 * @brief Signal replay module for reproducing captured IR signals
 *
 * Loads signal files from SD via Storage_LoadSignal(), decodes NEC
 * protocol, and replays via modulo_ir_send_nec().
 */

#ifndef _SIGNAL_REPLAY_H_
#define _SIGNAL_REPLAY_H_

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "signal_capture.h"

/*==================[types]==================================================*/

typedef enum {
    REPLAY_STATE_IDLE,
    REPLAY_STATE_LOADING,
    REPLAY_STATE_READY,
    REPLAY_STATE_PLAYING,
    REPLAY_STATE_ERROR
} ReplayState_t;

/*==================[external functions]=====================================*/

/**
 * @brief Replay Task
 * Blocks on task notification, loads signal from SD, decodes NEC, and sends
 * @param pvParameters Task parameters (unused)
 */
void vReplay_Task(void *pvParameters);

/**
 * @brief Queue a file for replay (call before xTaskNotifyGive)
 * @param filename Short filename (without SIGNAL_DIR path prefix)
 * @return pdPASS on success, pdFAIL if replay already in progress
 */
BaseType_t Replay_Start(const char *filename);

/**
 * @brief Stop/cancel replay
 */
void Replay_Stop(void);

/**
 * @brief Get replay state
 * @return Current replay state
 */
ReplayState_t Replay_GetState(void);

/**
 * @brief Get replay progress
 * @return Progress percentage (0-100)
 */
uint8_t Replay_GetProgress(void);

/*==================[end of file]============================================*/

#endif /* _SIGNAL_REPLAY_H_ */
