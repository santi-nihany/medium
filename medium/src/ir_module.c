/**
 * @file ir_module.c
 * @brief IR capture and transmit implementation
 *
 * Adapted from branch_ir modulo_IR_limpio.c.
 * - Capture: ISR-driven via PININT channel 0 + TIMER2 timestamps
 * - NEC decode: standard NEC protocol decoder
 * - NEC TX: blocking software-driven 38kHz carrier
 */

#include "ir_module.h"
#include <stdio.h>

/*==================[timer functions]=======================================*/

void ir_init(void)
{
    /* Configure IR RX pin as input with pull-up */
    gpioConfig(IR_INPUT_PIN, GPIO_INPUT);

    /* Configure IR TX pin as output, default inactive */
    gpioConfig(IR_TX_PIN, GPIO_OUTPUT);
    gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);

    /* Initialize TIMER2 as free-running µs counter */
    Chip_TIMER_Init(LPC_TIMER2);
    Chip_TIMER_PrescaleSet(LPC_TIMER2,
        (Chip_Clock_GetRate(CLK_MX_TIMER2) / 1000000) - 1);
    Chip_TIMER_Reset(LPC_TIMER2);
    Chip_TIMER_Enable(LPC_TIMER2);
}

uint32_t ir_getTimeUs(void)
{
    return Chip_TIMER_ReadCount(LPC_TIMER2);
}

static inline void ir_busyDelayUs(uint32_t us)
{
    uint32_t start = ir_getTimeUs();
    while ((ir_getTimeUs() - start) < us) {
        ;
    }
}

/*==================[NEC decoder]===========================================*/

bool ir_nec_decode(IRPulse_t *pulses, uint16_t count,
                   uint8_t *address, uint8_t *command)
{
    if (count < 4) return false;

    /* Validate header: pulse 0 = LOW ~9ms */
    if (!(pulses[0].level == 0 &&
          pulses[0].duration > 8500 &&
          pulses[0].duration < 9500)) {
        return false;
    }

    /* Validate header: pulse 1 = HIGH ~4.5ms */
    if (!(pulses[1].level == 1 &&
          pulses[1].duration > 4000 &&
          pulses[1].duration < 5000)) {
        return false;
    }

    uint32_t data = 0;

    /* Decode 32 bits starting at pulse 2 (pairs of LOW mark + HIGH space) */
    for (int i = 2; i + 1 < count; i += 2) {
        /* LOW mark ~560µs */
        if (!(pulses[i].level == 0 &&
              pulses[i].duration > 400 &&
              pulses[i].duration < 700)) {
            return false;
        }

        /* HIGH space determines bit value */
        uint32_t high = pulses[i + 1].duration;
        uint8_t bit;

        if (high > 400 && high < 700) {
            bit = 0;
        } else if (high > 1500 && high < 2000) {
            bit = 1;
        } else {
            return false;
        }

        data = (data >> 1) | ((uint32_t)bit << 31);  /* NEC sends LSB first */
    }

    uint8_t addr     =  data        & 0xFF;
    uint8_t addrInv  = (data >> 8)  & 0xFF;
    uint8_t cmd      = (data >> 16) & 0xFF;
    uint8_t cmdInv   = (data >> 24) & 0xFF;

    /* NEC verification */
    if (addr != (uint8_t)~addrInv) return false;
    if (cmd  != (uint8_t)~cmdInv)  return false;

    *address = addr;
    *command = cmd;

    return true;
}

/*==================[NEC transmitter]=======================================*/

void ir_send_nec_blocking(uint8_t addr, uint8_t cmd)
{
    uint8_t addrInv = ~addr;
    uint8_t cmdInv = ~cmd;
    uint32_t data = (uint32_t)addr |
                    ((uint32_t)addrInv << 8) |
                    ((uint32_t)cmd << 16) |
                    ((uint32_t)cmdInv << 24);
    bool carrierPhase = false;
    uint32_t start;

    gpioConfig(IR_TX_PIN, GPIO_OUTPUT);
    gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);

    /* Header MARK (carrier ON for 9ms) */
    __disable_irq();
    start = ir_getTimeUs();
    while ((ir_getTimeUs() - start) < NEC_HDR_MARK) {
        carrierPhase = !carrierPhase;
        gpioWrite(IR_TX_PIN, carrierPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
        ir_busyDelayUs(IR_CARRIER_HALF_US);
    }

    /* Header SPACE (4.5ms) */
    gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
    ir_busyDelayUs(NEC_HDR_SPACE);

    /* 32 data bits (LSB first) */
    for (int i = 0; i < 32; i++) {
        /* Bit MARK (carrier ON for 560µs) */
        start = ir_getTimeUs();
        while ((ir_getTimeUs() - start) < NEC_BIT_MARK) {
            carrierPhase = !carrierPhase;
            gpioWrite(IR_TX_PIN, carrierPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
            ir_busyDelayUs(IR_CARRIER_HALF_US);
        }

        /* Bit SPACE (560µs for 0, 1690µs for 1) */
        uint8_t bit = (data >> i) & 1;
        gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
        ir_busyDelayUs(bit ? NEC_ONE_SPACE : NEC_ZERO_SPACE);
    }

    /* Stop MARK (carrier ON for 560µs) */
    start = ir_getTimeUs();
    while ((ir_getTimeUs() - start) < NEC_STOP_MARK) {
        carrierPhase = !carrierPhase;
        gpioWrite(IR_TX_PIN, carrierPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
        ir_busyDelayUs(IR_CARRIER_HALF_US);
    }

    gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
    __enable_irq();
}

/*==================[end of file]===========================================*/
