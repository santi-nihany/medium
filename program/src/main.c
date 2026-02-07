/*
 * CC1101 Tool - EDU-CIAA con sAPI
 * Basado en my_c11_lib.ino para ESP32
 */

#include "c1101.h"
#include "chip.h"
#include "sapi.h"
#include <stdlib.h>
#include <string.h>

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

// Constantes
#define BUF_LENGTH 128
#define RECORDINGBUFFERSIZE 4096

// Variables de configuración
float freq = 433.92;
float band_width = 101;
const int lecturas_length = 4096;
int delay_us = 10;
const float data_rate = 5.0;
uint8_t modulation_type = MOD_ASK_OOK;

// Buffers
uint8_t lecturas[4096] = {0};
uint8_t bigrecordingbuffer[RECORDINGBUFFERSIZE] = {0};

// Modo de operación
bool_t auto_capture_mode = FALSE;
uint32_t cont = 1;

// Leer registros de C1101
// uint8_t c1101_readReg(uint8_t addr)
void printConfig() {
  char buffer[40];
  snprintf(buffer, sizeof(buffer), "\n=== CONFIGURACIÓN CC1101 ===\r\n");
  uartWriteString(UART_USB, buffer);
  snprintf(buffer, sizeof(buffer), "FREQ2: 0x%02X\r\n",
           c1101_readReg(CC1101_FREQ2));
  uartWriteString(UART_USB, buffer);
  snprintf(buffer, sizeof(buffer), "FREQ1: 0x%02X\r\n",
           c1101_readReg(CC1101_FREQ1));
  uartWriteString(UART_USB, buffer);
  snprintf(buffer, sizeof(buffer), "FREQ0: 0x%02X\r\n",
           c1101_readReg(CC1101_FREQ0));
  uartWriteString(UART_USB, buffer);
  snprintf(buffer, sizeof(buffer), "MDMCFG4: 0x%02X\r\n",
           c1101_readReg(CC1101_MDMCFG4));
  uartWriteString(UART_USB, buffer);
  snprintf(buffer, sizeof(buffer), "MDMCFG3: 0x%02X\r\n",
           c1101_readReg(CC1101_MDMCFG3));
  uartWriteString(UART_USB, buffer);
  snprintf(buffer, sizeof(buffer), "MDMCFG2: 0x%02X\r\n",
           c1101_readReg(CC1101_MDMCFG2));
  uartWriteString(UART_USB, buffer);
  snprintf(buffer, sizeof(buffer), "PKTCTRL0: 0x%02X\r\n",
           c1101_readReg(CC1101_PKTCTRL0));
  uartWriteString(UART_USB, buffer);
  snprintf(buffer, sizeof(buffer), "FREND0: 0x%02X\r\n",
           c1101_readReg(CC1101_FREND0));
  uartWriteString(UART_USB, buffer);
  snprintf(buffer, sizeof(buffer), "MARCSTATE: 0x%02X\r\n",
           c1101_readReg(CC1101_MARCSTATE) & 0x1F);
  uartWriteString(UART_USB, buffer);
  snprintf(buffer, sizeof(buffer), "===========================\r\n");
  uartWriteString(UART_USB, buffer);
}

// ============ IMPLEMENTACIÓN DE FUNCIONES GPIO ============

void c1101_initCustomGPIO(void) {
  // Configurar CS (P1_20)
  Chip_SCU_PinMuxSet(CC1101_CS_SCU_PORT, CC1101_CS_SCU_PIN,
                     (SCU_MODE_INACT | SCU_MODE_FUNC0));
  Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, CC1101_CS_GPIO_PORT,
                            CC1101_CS_GPIO_PIN);
  c1101_setCS(TRUE);

  // Configurar GDO0 como entrada (para recepción)
  Chip_SCU_PinMuxSet(CC1101_GDO0_SCU_PORT, CC1101_GDO0_SCU_PIN,
                     (SCU_MODE_INACT | SCU_MODE_INBUFF_EN | SCU_MODE_ZIF_DIS |
                      SCU_MODE_FUNC0));
  Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, CC1101_GDO0_GPIO_PORT,
                           CC1101_GDO0_GPIO_PIN);

  // Configurar GDO2 como entrada
  Chip_SCU_PinMuxSet(CC1101_GDO2_SCU_PORT, CC1101_GDO2_SCU_PIN,
                     (SCU_MODE_INACT | SCU_MODE_INBUFF_EN | SCU_MODE_ZIF_DIS |
                      SCU_MODE_FUNC0));
  Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, CC1101_GDO2_GPIO_PORT,
                           CC1101_GDO2_GPIO_PIN);
}

void c1101_setCS(bool_t state) {
  Chip_GPIO_SetPinState(LPC_GPIO_PORT, CC1101_CS_GPIO_PORT, CC1101_CS_GPIO_PIN,
                        state);
}

void c1101_setGDO0(bool_t state) {
  // Cambiar a salida si se necesita escribir
  Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, CC1101_GDO0_GPIO_PORT,
                            CC1101_GDO0_GPIO_PIN);
  Chip_GPIO_SetPinState(LPC_GPIO_PORT, CC1101_GDO0_GPIO_PORT,
                        CC1101_GDO0_GPIO_PIN, state);
}

bool_t c1101_getGDO0(void) {
  Chip_GPIO_SetPinDIRInput(LPC_GPIO_PORT, CC1101_GDO0_GPIO_PORT,
                           CC1101_GDO0_GPIO_PIN);
  return Chip_GPIO_GetPinState(LPC_GPIO_PORT, CC1101_GDO0_GPIO_PORT,
                               CC1101_GDO0_GPIO_PIN);
}

// ============ FUNCIONES DE UTILIDAD ============

void print_hex_byte(uint8_t value, bool_t highlight) {
  char buffer[10];
  if (highlight) {
    uartWriteString(UART_USB, "\x1B[31m"); // ANSI Red
  }
  snprintf(buffer, sizeof(buffer), "%02X", value);
  uartWriteString(UART_USB, buffer);
  if (highlight) {
    uartWriteString(UART_USB, "\x1B[0m"); // ANSI Reset
  }
  uartWriteString(UART_USB, " ");
}

// ============ FUNCIONES DE COMANDO ============

void cmd_setmhz(float mhz) {
  freq = mhz;
  c1101_setFrequency(freq);
  char buffer[50];
  uartWriteString(UART_USB, "\r\nFrecuencia: ");
  sprintf(buffer, "%.2f MHz\r\n", freq);
  uartWriteString(UART_USB, buffer);
}

void cmd_setmodulation(uint8_t m) {
  if (m > 4)
    m = 4;
  modulation_type = m;
  c1101_setModulation(m);
  uartWriteString(UART_USB, "\r\nModulación: ");
  switch (m) {
  case 0:
    uartWriteString(UART_USB, "2-FSK\r\n");
    break;
  case 1:
    uartWriteString(UART_USB, "GFSK\r\n");
    break;
  case 2:
    uartWriteString(UART_USB, "ASK/OOK\r\n");
    break;
  case 3:
    uartWriteString(UART_USB, "4-FSK\r\n");
    break;
  case 4:
    uartWriteString(UART_USB, "MSK\r\n");
    break;
  }
}

void cmd_setrxbw(float bw) {
  band_width = bw;
  c1101_setRxBW(bw);
  char buffer[50];
  uartWriteString(UART_USB, "\r\nRX Bandwidth: ");
  sprintf(buffer, "%.2f kHz\r\n", bw);
  uartWriteString(UART_USB, buffer);
}

void cmd_recraw(int sampling_delay) {
  if (sampling_delay <= 0) {
    uartWriteString(UART_USB, "Error: delay debe ser > 0\r\n");
    return;
  }

  delay_us = sampling_delay;

  // Configurar modo asíncrono
  c1101_setPktFormat(3); // Async mode
  c1101_setRxMode();

  char buffer[100];
  sprintf(buffer, "\r\nEsperando señal para grabar RAW (delay=%d us)...\r\n",
          delay_us);
  uartWriteString(UART_USB, buffer);

  // Esperar a que llegue señal (GDO0 HIGH)
  // while (c1101_getGDO0() == FALSE)
  //   ;

  uartWriteString(UART_USB, "¡Señal detectada! Grabando...\r\n");

  printConfig();

  // Grabar en buffer grande
  for (int i = 0; i < RECORDINGBUFFERSIZE; i++) {
    uint8_t receivedbyte = 0;
    for (int j = 7; j > -1; j--) {
      if (c1101_getGDO0()) {
        receivedbyte |= (1 << j);
      }
      delayInaccurateUs(delay_us);
    }
    bigrecordingbuffer[i] = receivedbyte;
  }

  uartWriteString(UART_USB, "\r\nGrabación RAW completa.\r\n");
  uartWriteString(UART_USB, "Datos capturados:\r\n");

  // Mostrar los datos en formato hex
  for (int i = 0; i < RECORDINGBUFFERSIZE; i++) {
    print_hex_byte(bigrecordingbuffer[i], bigrecordingbuffer[i] != 0);
    if ((i + 1) % 64 == 0)
      uartWriteString(UART_USB, "\r\n");
  }
  uartWriteString(UART_USB, "\r\n");
}

void mostrar_config(void) {
  char buffer[100];
  uartWriteString(UART_USB, "\r\n=== CONFIGURACIÓN ACTUAL ===\r\n");
  sprintf(buffer, "Frecuencia: %.2f MHz\r\n", freq);
  uartWriteString(UART_USB, buffer);

  uartWriteString(UART_USB, "Modulación: ");
  switch (modulation_type) {
  case 0:
    uartWriteString(UART_USB, "2-FSK\r\n");
    break;
  case 1:
    uartWriteString(UART_USB, "GFSK\r\n");
    break;
  case 2:
    uartWriteString(UART_USB, "ASK/OOK\r\n");
    break;
  case 3:
    uartWriteString(UART_USB, "4-FSK\r\n");
    break;
  case 4:
    uartWriteString(UART_USB, "MSK\r\n");
    break;
  }

  sprintf(buffer, "RX Bandwidth: %.2f kHz\r\n", band_width);
  uartWriteString(UART_USB, buffer);
  sprintf(buffer, "Delay: %d us\r\n", delay_us);
  uartWriteString(UART_USB, buffer);
  sprintf(buffer, "Buffer size: %d bytes\r\n", lecturas_length);
  uartWriteString(UART_USB, buffer);
  uartWriteString(UART_USB, "Modo: ");
  uartWriteString(UART_USB,
                  auto_capture_mode ? "Auto-captura\r\n" : "Comandos\r\n");
  uartWriteString(UART_USB, "============================\r\n\r\n");
}

void mostrar_ayuda(void) {
  uartWriteString(UART_USB, "\r\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n=="
                            "= COMANDOS DISPONIBLES ===\r\n");
  uartWriteString(UART_USB, "help         - Muestra esta ayuda\r\n");
  uartWriteString(UART_USB, "config       - Muestra configuración actual\r\n");
  uartWriteString(
      UART_USB,
      "setmhz <f>   - Configura frecuencia en MHz (ej: setmhz 315)\r\n");
  uartWriteString(UART_USB, "setmod <m>   - Configura modulación "
                            "0=2FSK,1=GFSK,2=ASK,3=4FSK,4=MSK\r\n");
  uartWriteString(
      UART_USB,
      "setrxbw <bw> - Configura RX bandwidth en kHz (ej: setrxbw 812.50)\r\n");
  uartWriteString(
      UART_USB,
      "recraw <d>   - Graba señal RAW con delay <d> en us (ej: recraw 10)\r\n");
  uartWriteString(UART_USB,
                  "capture      - Captura una señal (modo manual)\r\n");
  uartWriteString(UART_USB,
                  "auto         - Alterna modo auto-captura on/off\r\n");
  uartWriteString(UART_USB, "============================\r\n\r\n");
}

void realizar_captura(void) {
  char buffer[50];
  sprintf(buffer, "Captura %lu\r\n", cont++);
  uartWriteString(UART_USB, buffer);

  // Limpiar buffer
  for (int i = 0; i < lecturas_length; i++) {
    lecturas[i] = 0;
  }

  uartWriteString(UART_USB, "\r\n== Capturando señal.\r\n\r\n");

  // Captura de señal
  uint8_t currentByte = 0;
  int bitCount = 0;
  int byteIndex = 0;
  const int captured_bits = lecturas_length * 8;

  for (int i = 0; i < captured_bits; i++) {
    currentByte = (currentByte << 1) | (c1101_getGDO0() ? 1 : 0);
    bitCount++;

    if (bitCount == 8) {
      lecturas[byteIndex++] = currentByte;
      currentByte = 0;
      bitCount = 0;
    }

    delayInaccurateUs(delay_us);
  }

  uartWriteString(UART_USB, "\r\n== Captura finalizada.\r\n\r\n");

  if (bitCount > 0) {
    lecturas[byteIndex] = currentByte << (8 - bitCount);
  }

  // Verificar si hay datos útiles
  bool_t todos_ceros = TRUE;
  for (int i = 0; i < lecturas_length; i++) {
    if (lecturas[i] != 0 && lecturas[i] != 0xFF) {
      todos_ceros = FALSE;
      break;
    }
  }

  if (!todos_ceros) {
    for (int i = 0; i < lecturas_length; i++) {
      print_hex_byte(lecturas[i], lecturas[i] != 0);
      if ((i + 1) % 64 == 0)
        uartWriteString(UART_USB, "\r\n");
    }
    uartWriteString(UART_USB, "\r\n");
  }
}

void procesar_comando(char *cmdline) {
  char *command = strtok(cmdline, " ");
  if (command == NULL)
    return;

  if (strcmp(command, "help") == 0) {
    mostrar_ayuda();

  } else if (strcmp(command, "config") == 0) {
    mostrar_config();

  } else if (strcmp(command, "setmhz") == 0) {
    char *param = strtok(NULL, " ");
    if (param != NULL) {
      float mhz = atof(param);
      cmd_setmhz(mhz);
    } else {
      uartWriteString(UART_USB,
                      "Error: falta parámetro (ej: setmhz 433.92)\r\n");
    }

  } else if (strcmp(command, "setmod") == 0) {
    char *param = strtok(NULL, " ");
    if (param != NULL) {
      uint8_t m = atoi(param);
      cmd_setmodulation(m);
    } else {
      uartWriteString(UART_USB, "Error: falta parámetro (ej: setmod 2)\r\n");
    }

  } else if (strcmp(command, "setrxbw") == 0) {
    char *param = strtok(NULL, " ");
    if (param != NULL) {
      float bw = atof(param);
      cmd_setrxbw(bw);
    } else {
      uartWriteString(UART_USB,
                      "Error: falta parámetro (ej: setrxbw 812.50)\r\n");
    }

  } else if (strcmp(command, "recraw") == 0) {
    char *param = strtok(NULL, " ");
    if (param != NULL) {
      int d = atoi(param);
      cmd_recraw(d);
    } else {
      uartWriteString(UART_USB, "Error: falta parámetro (ej: recraw 10)\r\n");
    }

  } else if (strcmp(command, "capture") == 0) {
    realizar_captura();

  } else if (strcmp(command, "auto") == 0) {
    auto_capture_mode = !auto_capture_mode;
    uartWriteString(UART_USB, "\r\nModo auto-captura: ");
    uartWriteString(UART_USB, auto_capture_mode ? "ON\r\n" : "OFF\r\n");

  } else {
    uartWriteString(UART_USB,
                    "Comando desconocido. Usa 'help' para ver comandos.\r\n");
  }
}

int main(void) {
  // Inicialización del sistema
  boardConfig();
  uartConfig(UART_USB, 115200);
  c1101_initCustomGPIO();
  spiConfig(SPI0);

  uartWriteString(UART_USB, "\r\n=================================\r\n");
  uartWriteString(UART_USB, "CC1101 Tool - Librería C1101\r\n");
  uartWriteString(UART_USB, "=================================\r\n\r\n");

  // Inicializar CC1101
  if (c1101_detect()) {
    uartWriteString(UART_USB, "CC1101: OK\r\n");
  } else {
    uartWriteString(UART_USB, "CC1101: ERROR\r\n");
    while (1)
      ;
  }

  c1101_init();
  c1101_setSidle();
  c1101_setModulation(modulation_type);
  c1101_setFrequency(freq);
  c1101_setRxBW(band_width);
  c1101_setDataRate(data_rate);
  c1101_setPktFormat(3);     // Async mode
  c1101_setSyncMode(0);      // No sync
  c1101_setWhiteData(FALSE); // No whitening
  c1101_setCrc(FALSE);       // No CRC

  c1101_setRxMode();

  uartWriteString(UART_USB, "Setup completado.\r\n");
  mostrar_config();
  mostrar_ayuda();

  printConfig();

  // Buffer para comandos
  static char buffer[BUF_LENGTH];
  static int length = 0;

  while (1) {
    // Procesar comandos por serial
    uint8_t data;

    if (uartReadByte(UART_USB, &data)) {
      if (data == '\b' || data == '\177') { // BS y DEL
        if (length) {
          length--;
          uartWriteString(UART_USB, "\b \b");
        }
      } else if (data == '\r' || data == '\n') {
        uartWriteString(UART_USB, "\r\n");
        buffer[length] = '\0';
        if (length)
          procesar_comando(buffer);
        length = 0;
        printConfig();
      } else if (length < BUF_LENGTH - 1) {
        buffer[length++] = data;
        uartWriteByte(UART_USB, data);
      }
    }

    // Modo auto-captura
    if (auto_capture_mode) {
      delay(2000);
      realizar_captura();
    }
  }

  return 0;
}
