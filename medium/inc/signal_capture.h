/**
 * @file signal_capture.h
 * @brief Signal capture module for IR and RF signals
 *
 * IR capture: Polling-based via modulo_ir_capture() (triggered by task notification)
 * RF capture: Polling-based via CC1101 GDO0 pin (triggered by task notification)
 */

#ifndef _SIGNAL_CAPTURE_H_
#define _SIGNAL_CAPTURE_H_

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"

/*==================[macros and definitions]=================================*/

#define SIGNAL_MODE_IR        0
#define SIGNAL_MODE_RF        1

/*==================[types]==================================================*/

/**
 * @brief Signal data packet structure
 */
typedef struct {
    uint8_t mode;           /* IR or RF */
    uint32_t timestamp_ms;   /* Capture start timestamp */
    uint32_t sample_count;  /* Number of samples */
    uint8_t data[];          /* Variable length data */
} SignalPacket_t;

/*==================[RF capture config]=====================================*/

/* Forward reference — full definition in rf_capture.h */
#include "rf_capture.h"

/*==================[external functions]=====================================*/

/**
 * @brief IR Signal Capture Task
 * Polls IR signal via modulo_ir_capture(), packages into SignalPacket_t
 * and sends to Storage Queue
 * @param pvParameters Task parameters (unused)
 */
void vSignalCaptureIR_Task(void *pvParameters);

/**
 * @brief RF Signal Capture Task
 * Configures CC1101, polls GDO0 via rf_capture_raw(), packages into
 * SignalPacket_t and sends to Storage Queue
 * @param pvParameters Task parameters (unused)
 */
void vSignalCaptureRF_Task(void *pvParameters);

/**
 * @brief Check if capture is active
 * @param mode SIGNAL_MODE_IR or SIGNAL_MODE_RF
 * @return pdTRUE if active, pdFALSE otherwise
 */
BaseType_t SignalCapture_IsActive(uint8_t mode);

/*==================[end of file]============================================*/

#endif /* _SIGNAL_CAPTURE_H_ */
