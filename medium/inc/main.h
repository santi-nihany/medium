/**
 * @file main.h
 * @brief Public interface for main module
 */

#ifndef MEDIUM_MAIN_H_
#define MEDIUM_MAIN_H_

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Task handles (defined in main.c, used by UI controller for notifications) */
extern TaskHandle_t xTaskSignalCaptureIR;
extern TaskHandle_t xTaskSignalCaptureRF;
extern TaskHandle_t xTaskReplay;

#ifdef __cplusplus
}
#endif

#endif /* MEDIUM_MAIN_H_ */
