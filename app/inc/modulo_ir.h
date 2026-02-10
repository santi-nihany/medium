#ifndef MODULO_IR_H
#define MODULO_IR_H

#include "sapi.h"
#include "sapi_timer.h"
#include "chip.h"
#include <string.h>


// Configuracion de pines y parametros 
#define IR_INPUT_PIN          GPIO7
#define IR_IRQ_CHANNEL        0
#define TIMEOUT_US            40000
#define MAX_PULSES            200
#define MIN_PULSE_US          300
#define SAMPLE_US             30
#define CARRIER_FREQ_HZ       38000
#define IR_TX_PIN GPIO5
#define IR_ACTIVE_LEVEL 1

typedef struct {
   uint8_t level;        // Nivel logico (0 o 1)
   uint32_t duration;    // Duracion en microsegundos
} IRPulse_t;

// Inicializa hardware necesario (GPIO, timers, driver TX)
void modulo_ir_init(void);

// Captura una trama IR en el buffer proporcionado. Devuelve true si hay trama. | outcount cantidad de pulsos leidos
bool modulo_ir_capture(IRPulse_t *outBuffer, uint16_t *outCount);

// Decodificador NEC 
bool modulo_ir_decode(IRPulse_t *pulses, uint16_t count, uint8_t *address, uint8_t *command);

// Envia un comando NEC 
void modulo_ir_send_nec(uint8_t addr, uint8_t cmd);

#endif // MODULO_IR_H
