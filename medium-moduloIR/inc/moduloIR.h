#ifndef MODULO_IR_H
#define MODULO_IR_H

#include <stdint.h>
#include <stdbool.h>

/* Inicializa todo el subsistema IR (RX + TX) */
void moduloIR_Init(void);

/* Debe llamarse periódicamente desde el main loop */
bool moduloIR_TramaDisponible(void);

/* Intenta decodificar la última trama NEC */
bool moduloIR_GetNEC(uint8_t *addr, uint8_t *cmd);

/* Envía una trama NEC */
void moduloIR_SendNEC(uint8_t addr, uint8_t cmd);

#endif