/**
 * @file signal_capture.h
 * @brief Signal capture module for IR and RF signals
 * 
 * This module handles real-time capture of IR and RF signals using
 * hardware interrupts, stores raw timings in StreamBuffers, and packages
 * them for storage.
 */

#ifndef _SIGNAL_CAPTURE_H_
#define _SIGNAL_CAPTURE_H_

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "stream_buffer.h"

/*==================[macros and definitions]=================================*/

#define SIGNAL_MODE_IR        0
#define SIGNAL_MODE_RF        1

/* Maximum capture duration in milliseconds */
#define MAX_CAPTURE_DURATION  5000

/* StreamBuffer watermark for triggering packet creation */
#define STREAM_BUFFER_WATERMARK  256

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
 * Consumes data from StreamBuffer, packages it into packets and sends to Storage Queue
 * @param pvParameters Task parameters (unused)
 */
void vSignalCaptureIR_Task(void *pvParameters);

/**
 * @brief RF Signal Capture Task
 * Consumes data from StreamBuffer, packages it into packets and sends to Storage Queue
 * @param pvParameters Task parameters (unused)
 */
void vSignalCaptureRF_Task(void *pvParameters);

/**
 * @brief Start signal capture
 * @param mode SIGNAL_MODE_IR or SIGNAL_MODE_RF
 */
void SignalCapture_Start(uint8_t mode);

/**
 * @brief Stop signal capture
 * @param mode SIGNAL_MODE_IR or SIGNAL_MODE_RF
 */
void SignalCapture_Stop(uint8_t mode);

/**
 * @brief Check if capture is active
 * @param mode SIGNAL_MODE_IR or SIGNAL_MODE_RF
 * @return pdTRUE if active, pdFALSE otherwise
 */
BaseType_t SignalCapture_IsActive(uint8_t mode);

/*==================[end of file]============================================*/

#endif /* _SIGNAL_CAPTURE_H_ */

