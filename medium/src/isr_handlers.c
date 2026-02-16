/**
 * @file isr_handlers.c
 * @brief Interrupt Service Routines for hardware signal capture
 *
 * ISRs:
 * - GPIO_IRQHandler (PIN_INT0): IR signal edge detection via PININT channel 0
 *   Reads TIMER2 timestamp, packs (delta_us | level) and sends to xStreamBufferIR
 */

#include "signal_capture.h"
#include "ir_module.h"
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "queue.h"
#include "ui_controller.h"
#include "main.h"

/*==================[external data]==========================================*/

extern StreamBufferHandle_t xStreamBufferIR;
extern QueueHandle_t xUICommandQueue;

/*==================[ISR implementations]===================================*/

/**
 * @brief PIN_INT0 ISR — IR signal edge detection
 *
 * Called on both rising and falling edges of the IR receiver output.
 * Reads TIMER2 for µs timestamp, calculates delta since last edge,
 * packs {delta_us(24bits), level(8bits)} as uint32_t and sends to
 * xStreamBufferIR for the capture task.
 */
void GPIO0_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    static uint32_t last_timestamp = 0;

    /* Read current timestamp from TIMER2 (µs resolution) */
    uint32_t now = Chip_TIMER_ReadCount(LPC_TIMER2);
    uint32_t delta = now - last_timestamp;
    last_timestamp = now;

    /* Read current signal level on IR RX pin */
    uint8_t level = Chip_GPIO_GetPinState(LPC_GPIO_PORT,
                                           IR_RX_GPIO_PORT, IR_RX_GPIO_PIN);

    /* Pack: lower 24 bits = delta_us, upper 8 bits = level */
    uint32_t sample = (delta & 0x00FFFFFF) | ((uint32_t)level << 24);

    /* Send to StreamBuffer (non-blocking from ISR) */
    if (xStreamBufferIR != NULL) {
        xStreamBufferSendFromISR(xStreamBufferIR, &sample, sizeof(uint32_t),
                                  &xHigherPriorityTaskWoken);
    }

    /* Clear interrupt status */
    Chip_PININT_ClearIntStatus(LPC_GPIO_PIN_INT, PININTCH(0));

    /* Yield if higher priority task was woken */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*==================[end of file]===========================================*/
