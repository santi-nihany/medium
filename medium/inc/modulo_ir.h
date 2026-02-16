#ifndef MODULO_IR_H
#define MODULO_IR_H

#include "sapi.h"
#include "sapi_timer.h"
#include "chip.h"
#include <string.h>

/*==================[pin and parameter configuration]======================*/

#define IR_INPUT_PIN          GPIO7
#define IR_IRQ_CHANNEL        0
#define TIMEOUT_US            40000
#define MAX_PULSES            200
#define MIN_PULSE_US          300
#define SAMPLE_US             30
#define CARRIER_FREQ_HZ       38000
#define IR_TX_PIN             GPIO5
#define IR_ACTIVE_LEVEL       1

/* Actual GPIO port/pin for IR RX (needed for PININT and ISR reads).
 * GPIO7 on EDU-CIAA — must match the LPC4337 mapping for sAPI GPIO7.
 * TODO: Confirm from sAPI headers */
#define IR_RX_GPIO_PORT       5
#define IR_RX_GPIO_PIN        15

/*==================[types]=================================================*/

typedef struct {
   uint8_t level;        // Nivel logico (0 o 1)
   uint32_t duration;    // Duracion en microsegundos
} IRPulse_t;

/*==================[functions]=============================================*/

// Inicializa hardware necesario (GPIO, timers, driver TX)
// NOTE: Does NOT call boardConfig() — caller must do that first.
void modulo_ir_init(void);

// Captura una trama IR en el buffer proporcionado.
// Devuelve true si hay trama. outCount = cantidad de pulsos leidos.
// WARNING: Blocking (up to 3s). In RTOS, prefer ISR-driven capture via xStreamBufferIR.
bool modulo_ir_capture(IRPulse_t *outBuffer, uint16_t *outCount);

// Decodificador NEC
bool modulo_ir_decode(IRPulse_t *pulses, uint16_t count, uint8_t *address, uint8_t *command);

// Envia un comando NEC (blocking, ~110ms with __disable_irq)
void modulo_ir_send_nec(uint8_t addr, uint8_t cmd);

// Timer auxiliar microsegundos
uint32_t getTimeUs(void);
void resetTimeUs(void);

// Carrier 38kHz via Timer_0 callback (non-blocking)
void irCarrierOn(void);
void irCarrierOff(void);

#endif // MODULO_IR_H
