/**
 * @file ir_module.c
 * @brief IR capture and transmit implementation
 *
 * Adapted from branch_ir modulo_ir.c.
 * - Timer: TIMER2 free-running µs counter for timestamps and delays
 * - Capture (polling): ir_capture_polling() blocks and records IRPulse_t array
 * - Capture (RTOS): ISR-driven via PININT ch0 → xStreamBufferIR (see isr_handlers.c)
 * - NEC decode: standard NEC protocol decoder
 * - NEC TX: blocking software-driven 38kHz carrier (ir_send_nec_blocking)
 * - Carrier: Timer_0 callback for non-blocking 38kHz carrier (ir_carrier_on/off)
 */

#include "ir_module.h"
#include <stdio.h>

/*==================[internal data]=========================================*/

/* Polling capture state */
static IRPulse_t pulseBuffer[IR_MAX_PULSES];
static uint16_t pulseCount = 0;
static uint8_t lastLevel = 1;
static uint32_t lastTime = 0;
static bool tramaCompleta = false;
static bool captureStarted = false;

/* Carrier / TX internals */
static volatile bool carrierEnabled = false;
static volatile bool carrierPhase = false;

/*==================[timer functions]=======================================*/

void ir_init(void)
{
    /* Configure IR RX pin as input */
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

void ir_resetTimeUs(void)
{
    Chip_TIMER_Reset(LPC_TIMER2);
}

static inline void ir_busyDelayUs(uint32_t us)
{
    uint32_t start = ir_getTimeUs();
    while ((ir_getTimeUs() - start) < us) {
        ;
    }
}

/*==================[carrier (Timer_0 callback)]============================*/

/**
 * @brief Timer_0 callback for 38kHz carrier generation
 * Called every ~13µs (half-period). Toggles IR TX pin when carrier is enabled.
 */
static void ir_carrierCallback(void *ptr)
{
    (void)ptr;
    if (carrierEnabled) {
        carrierPhase = !carrierPhase;
        gpioWrite(IR_TX_PIN, carrierPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
    } else {
        gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
    }
}

void ir_carrier_on(void)
{
    carrierEnabled = true;
    Timer_Init(
        0,
        Timer_microsecondsToTicks(IR_CARRIER_HALF_US),
        ir_carrierCallback
    );
}

void ir_carrier_off(void)
{
    carrierEnabled = false;
    Timer_DeInit(0);
    gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
}

/*==================[polling capture]=======================================*/

bool ir_capture_polling(IRPulse_t *outBuffer, uint16_t *outCount)
{
    /* Wait for first edge (level change) */
    uint32_t waitTimeout = 0;
    uint8_t cur = gpioRead(IR_INPUT_PIN);
    while (cur == lastLevel) {
        delayInaccurateUs(IR_SAMPLE_US);
        waitTimeout += IR_SAMPLE_US;
        if (waitTimeout > 3000000) break;  /* 3s timeout */
        cur = gpioRead(IR_INPUT_PIN);
    }

    if (cur != lastLevel) {
        captureStarted = true;
        lastLevel = cur;
        pulseCount = 0;
        lastTime = ir_getTimeUs();

        /* Capture pulse sequence until frame timeout */
        while (true) {
            uint8_t level = gpioRead(IR_INPUT_PIN);
            uint32_t now = ir_getTimeUs();

            /* Detect edge (level change) */
            if (level != lastLevel) {
                uint32_t delta = now - lastTime;

                /* Filter out short pulses (glitch rejection) */
                if (delta >= IR_MIN_PULSE_US) {
                    if (pulseCount < IR_MAX_PULSES && delta < 60000) {
                        pulseBuffer[pulseCount].duration = delta;
                        pulseBuffer[pulseCount].level = lastLevel;
                        pulseCount++;
                    }
                    lastTime = now;
                } else {
                    lastTime = now;
                }
                lastLevel = level;
            }

            /* Check for end of frame (no edges for TIMEOUT_US) */
            if (pulseCount > 3 && (now - lastTime) > IR_TIMEOUT_US) {
                tramaCompleta = true;
            }

            if (tramaCompleta) break;

            delayInaccurateUs(IR_SAMPLE_US);
        }

        /* Copy captured data to output buffer and reset state */
        if (tramaCompleta) {
            uint16_t toCopy = pulseCount < IR_MAX_PULSES ? pulseCount : IR_MAX_PULSES;
            memcpy(outBuffer, pulseBuffer, sizeof(IRPulse_t) * toCopy);
            *outCount = toCopy;

            tramaCompleta = false;
            pulseCount = 0;
            lastLevel = 1;
            captureStarted = false;
            return true;
        }
    }

    return false;
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

    /* NEC verification: address and command must match their inversions */
    if (addr != (uint8_t)~addrInv) return false;
    if (cmd  != (uint8_t)~cmdInv)  return false;

    *address = addr;
    *command = cmd;

    return true;
}

/*==================[NEC transmitter (blocking)]============================*/

void ir_send_nec_blocking(uint8_t addr, uint8_t cmd)
{
    uint8_t addrInv = ~addr;
    uint8_t cmdInv = ~cmd;
    uint32_t data = (uint32_t)addr |
                    ((uint32_t)addrInv << 8) |
                    ((uint32_t)cmd << 16) |
                    ((uint32_t)cmdInv << 24);
    bool localPhase = false;
    uint32_t start;

    gpioConfig(IR_TX_PIN, GPIO_OUTPUT);
    gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);

    /* Header MARK (carrier ON for 9ms) */
    __disable_irq();
    start = ir_getTimeUs();
    while ((ir_getTimeUs() - start) < NEC_HDR_MARK) {
        localPhase = !localPhase;
        gpioWrite(IR_TX_PIN, localPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
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
            localPhase = !localPhase;
            gpioWrite(IR_TX_PIN, localPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
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
        localPhase = !localPhase;
        gpioWrite(IR_TX_PIN, localPhase ? IR_ACTIVE_LEVEL : (1 - IR_ACTIVE_LEVEL));
        ir_busyDelayUs(IR_CARRIER_HALF_US);
    }

    gpioWrite(IR_TX_PIN, 1 - IR_ACTIVE_LEVEL);
    __enable_irq();
}

/*==================[end of file]===========================================*/
