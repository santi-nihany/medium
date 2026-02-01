/**
 * @file mock_signal_generator.h
 * @brief Mock signal generator for testing IPC without hardware
 *
 * Simulates ISR behavior by feeding StreamBuffers with synthetic data.
 * Used to test the full data flow: Generator → StreamBuffer → Capture → Queue → Storage
 */

#ifndef MOCK_SIGNAL_GENERATOR_H
#define MOCK_SIGNAL_GENERATOR_H

#include "FreeRTOS.h"
#include "task.h"

/*==================[macros and definitions]=================================*/

/* Number of samples to generate per burst */
#define MOCK_SAMPLES_PER_BURST    64

/* Delay between bursts (simulates signal capture timing) */
#define MOCK_BURST_DELAY_MS       5000

/* Simulated delta time between edges (in timer ticks) */
#define MOCK_DELTA_TICKS          500

/*==================[external functions declaration]=========================*/

/**
 * @brief Mock signal generator task
 *
 * Periodically generates synthetic signal data and feeds it to
 * xStreamBufferIR, simulating what the IR_ISR_Handler would do.
 *
 * @param pvParameters Task parameters (unused)
 */
void vMockSignalGenerator_Task(void *pvParameters);

#endif /* MOCK_SIGNAL_GENERATOR_H */
