/**
 * @file ir_module.h
 * @brief IR capture and transmit module
 *
 * Adapted from branch_ir modulo_ir.h / modulo_IR_limpio.c.
 * ISR-driven capture using PININT + TIMER2, timer-driven NEC TX.
 */

#ifndef _IR_MODULE_H_
#define _IR_MODULE_H_

#include "sapi.h"
#include "chip.h"
#include <stdint.h>
#include <stdbool.h>

/*==================[IR pin configuration]=================================*/

#define IR_INPUT_PIN       GPIO7   /* sAPI alias for IR receiver (KY-022) */
#define IR_TX_PIN          GPIO5   /* sAPI alias for IR LED driver */

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

typedef enum {
    IR_TX_IDLE,
    IR_TX_HDR_MARK,
    IR_TX_HDR_SPACE,
    IR_TX_BIT_MARK,
    IR_TX_BIT_SPACE,
    IR_TX_STOP
} ir_tx_state_t;

/*==================[functions]=============================================*/

/**
 * @brief Initialize IR module hardware
 * Configures GPIO7 as input (IR RX), GPIO5 as output (IR TX),
 * TIMER2 as free-running µs counter
 */
void ir_init(void);

/**
 * @brief Get current timer value in microseconds
 * @return Microsecond count from TIMER2
 */
uint32_t ir_getTimeUs(void);

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
 * Uses TIMER2 busy-wait for carrier timing. Disables interrupts briefly
 * during critical carrier sections for precise 38kHz timing.
 * @param addr NEC address byte
 * @param cmd NEC command byte
 */
void ir_send_nec_blocking(uint8_t addr, uint8_t cmd);

/*==================[end of file]===========================================*/

#endif /* _IR_MODULE_H_ */
