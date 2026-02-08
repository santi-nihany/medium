#ifndef BOARD_H
#define BOARD_H

#include "main.h"

// --- Mapeo de Pines ---
// Pin 16 JP2 = TXD1 = P1_20 del LPC4337 = GPIO0[15]
#define CC1101_CS_SCU_PORT 1
#define CC1101_CS_SCU_PIN 20
#define CC1101_CS_GPIO_PORT 0
#define CC1101_CS_GPIO_PIN 15

// Pin 32 JP2 = GPIO1 = P0_0 del LPC4337 = GPIO0[0] (GDO0)
#define CC1101_GDO0_SCU_PORT 6
#define CC1101_GDO0_SCU_PIN 4
#define CC1101_GDO0_GPIO_PORT 3
#define CC1101_GDO0_GPIO_PIN 3

// Pin adicional para GDO2 (si se necesita)
#define CC1101_GDO2_SCU_PORT 6
#define CC1101_GDO2_SCU_PIN 1
#define CC1101_GDO2_GPIO_PORT 3
#define CC1101_GDO2_GPIO_PIN 0

// == Variables C1101 =======================================
// Constantes
#define BUF_LENGTH 128
#define RECORDINGBUFFERSIZE 4096

// Variables de configuración
extern float freq;
extern float band_width;
extern int delay_us;
extern uint8_t modulation_type;
extern uint8_t recordingbuffer[RECORDINGBUFFERSIZE];
// ==========================================================

// Variables para captura de señal
typedef struct {
  float freq_mhz;
  uint8_t modulation_mode;
  float bandwidth_khz;
  uint32_t delay_us;
  uint16_t data_length; // Cantidad de bytes válidos
  uint8_t *data_ptr;    // Puntero al buffer (NO copiar 4KB cada vez)
} capture_t;

// === FUNCIONES GPIO ===
void c1101_initCustomGPIO(void);
void c1101_setCS(bool_t state);
void c1101_setGDO0(bool_t state);
bool_t c1101_getGDO0(void);

// === FUNCIONES PARA USAR EL MODULO C1101 ===
void shield_c1101Setup(void);
void cmd_setmhz(float mhz);
void cmd_setmodulation(uint8_t m);
void cmd_setrxbw(float bw);
// void cmd_recraw(int sampling_delay);
bool_t cmd_recraw(int sampling_delay, capture_t *capture_out);
void mostrar_config(void);
void mostrar_registros(void);

// ============ FUNCIONES DE UTILIDAD ============
void print_hex_byte(uint8_t value, bool_t highlight);

#endif