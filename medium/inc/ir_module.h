/**
 * @file ir_module.h
 * @brief IR capture and transmit module
 *
 * Adapted from branch_ir modulo_ir.h / modulo_ir.c.
 * - Polling capture: ir_capture_polling() captures IRPulse_t array (blocking)
 * - ISR-driven capture: PININT + TIMER2 feeds xStreamBufferIR (RTOS primary)
 * - NEC decode/encode
 * - Timer-based carrier via sAPI Timer_0 callback
 */

#ifndef _IR_MODULE_H_
#define _IR_MODULE_H_

#include "sapi.h"
#include "sapi_timer.h"
#include "chip.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/*==================[IR pin configuration]=================================*/

#define IR_INPUT_PIN       GPIO7   /* sAPI alias for IR receiver (KY-022) */
#define IR_TX_PIN          GPIO5   /* sAPI alias for IR LED driver */
#define IR_IRQ_CHANNEL     0       /* PININT channel for IR RX edge detection */

/* Actual GPIO port/pin for IR RX (needed for PININT and ISR reads)
 * GPIO7 on EDU-CIAA = pin varies by sAPI mapping. These must match
 * the actual LPC4337 GPIO port/pin for the sAPI GPIO7 alias.
 * TODO: Confirm from sAPI headers — these are the most common mapping */
#define IR_RX_GPIO_PORT    5
#define IR_RX_GPIO_PIN     15

/* IR TX active level */
#define IR_ACTIVE_LEVEL    1

/*==================[timing constants]=====================================*/

#define IR_CARRIER_FREQ_HZ  38000
#define IR_CARRIER_HALF_US  (500000 / IR_CARRIER_FREQ_HZ)  /* ~13µs */

#define IR_TIMEOUT_US       40000   /* 40ms inactivity = end of frame */
#define IR_MAX_PULSES       200
#define IR_MIN_PULSE_US     300     /* Glitch filter threshold */
#define IR_SAMPLE_US        30      /* Polling sample interval (µs) */

/* NEC protocol timings (µs) */
#define NEC_HDR_MARK    9000
#define NEC_HDR_SPACE   4500
#define NEC_BIT_MARK     560
#define NEC_ONE_SPACE   1690
#define NEC_ZERO_SPACE   560
#define NEC_STOP_MARK    560

/*==================[types]=================================================*/

typedef struct {
    uint8_t level;
    uint32_t duration;
} IRPulse_t;

/*==================[functions]=============================================*/

/**
 * @brief Initialize IR module hardware
 * Configures GPIO7 as input (IR RX), GPIO5 as output (IR TX),
 * TIMER2 as free-running µs counter.
 * NOTE: Does NOT call boardConfig() — caller must do that first.
 */
void ir_init(void);

/**
 * @brief Get current timer value in microseconds
 * @return Microsecond count from TIMER2
 */
uint32_t ir_getTimeUs(void);

/**
 * @brief Reset TIMER2 counter to zero
 */
void ir_resetTimeUs(void);

/**
 * @brief Polling-based IR capture into IRPulse_t array
 *
 * Blocks until an IR frame is received or 3s timeout.
 * Polls GPIO7 every 30µs, records level transitions.
 * WARNING: This is blocking — in RTOS context, the ISR-driven
 * capture (PININT → xStreamBufferIR → CaptureIR task) is preferred.
 *
 * @param outBuffer Array of at least IR_MAX_PULSES elements
 * @param outCount  Output: number of pulses captured
 * @return true if a complete frame was captured
 */
bool ir_capture_polling(IRPulse_t *outBuffer, uint16_t *outCount);

/**
 * @brief Decode NEC protocol from pulse buffer
 * @param pulses Array of captured pulses
 * @param count Number of pulses
 * @param address Output: decoded NEC address
 * @param command Output: decoded NEC command
 * @return true if valid NEC frame, false otherwise
 */
bool ir_nec_decode(IRPulse_t *pulses, uint16_t count,
                   uint8_t *address, uint8_t *command);

/**
 * @brief Send NEC command (blocking, software-driven carrier)
 * Uses TIMER2 busy-wait for carrier timing. Disables interrupts
 * during the entire ~110ms NEC transmission for precise 38kHz timing.
 * @param addr NEC address byte
 * @param cmd NEC command byte
 */
void ir_send_nec_blocking(uint8_t addr, uint8_t cmd);

/**
 * @brief Enable 38kHz carrier on IR TX pin via Timer_0 callback
 * Non-blocking — carrier runs autonomously via hardware timer interrupt.
 */
void ir_carrier_on(void);

/**
 * @brief Disable carrier and set IR TX pin inactive
 */
void ir_carrier_off(void);

/*==================[end of file]===========================================*/

#endif /* _IR_MODULE_H_ */
