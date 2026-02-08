#include "shield.h"

// Variables generales
float freq = 433.92;
float band_width = 101;
int delay_us = 10;
uint8_t modulation_type = MOD_ASK_OOK;
uint8_t recordingbuffer[RECORDINGBUFFERSIZE] = {0};

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

// ====== IMPLEMENTACIÓN DE FUNCIONES PARA USAR EL MODULO C1101 ======
void shield_c1101Setup(void) {
  c1101_init();
  c1101_setSidle();
  c1101_setModulation(modulation_type);
  c1101_setFrequency(freq);
  c1101_setRxBW(band_width);
  c1101_setDataRate(5.0);
  c1101_setPktFormat(3);     // Async mode
  c1101_setSyncMode(0);      // No sync
  c1101_setWhiteData(FALSE); // No whitening
  c1101_setCrc(FALSE);       // No CRC

  c1101_setRxMode();
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
    m = 2;
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
  char buffer[80];
  sprintf(buffer, "\r\nRX Bandwidth: %.2f kHz\r\n", bw);
  uartWriteString(UART_USB, buffer);
}

/* Implementación vieja
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
  uint32_t timeout = 0;
  while (c1101_getGDO0() == FALSE) {
    delayInaccurateUs(delay_us);
    timeout += delay_us;
    if (timeout > 3000000) { // Timeout de 3 segundos
      uartWriteString(UART_USB, "Timeout: No se detectó señal.\r\n");
      return;
    }
  }

  uartWriteString(UART_USB, "¡Señal detectada! Grabando...\r\n");

  // Grabar en buffer grande
  for (int i = 0; i < RECORDINGBUFFERSIZE; i++) {
    uint8_t receivedbyte = 0;
    for (int j = 7; j > -1; j--) {
      if (c1101_getGDO0()) {
        receivedbyte |= (1 << j);
        // DATA :  { {2, 0}, FUNC4, {5, 0} },   // LEDR    LED0_R
        Chip_GPIO_SetPinState(LPC_GPIO_PORT, 5, 0, ON);
        // gpioWrite(LED1, ON);
      } else {
        Chip_GPIO_SetPinState(LPC_GPIO_PORT, 5, 0, OFF);
        // gpioWrite(LED1, OFF);
      }
      delayInaccurateUs(delay_us);
    }
    recordingbuffer[i] = receivedbyte;
  }
  Chip_GPIO_SetPinState(LPC_GPIO_PORT, 5, 0, OFF); // apagar led

  uartWriteString(UART_USB, "\r\nGrabación RAW completa.\r\n");
  uartWriteString(UART_USB, "Datos capturados:\r\n");

  // Mostrar los datos en formato hex
  for (int i = 0; i < RECORDINGBUFFERSIZE; i++) {
    print_hex_byte(recordingbuffer[i], recordingbuffer[i] != 0);
    if ((i + 1) % 48 == 0)
      uartWriteString(UART_USB, "\r\n");
  }
  uartWriteString(UART_USB, "\r\n");

  // TODO: retornar estructura CAPTURE_T con datos y configuración
} */

bool_t cmd_recraw(int sampling_delay, capture_t *capture_out) {
  if (sampling_delay <= 0) {
    uartWriteString(UART_USB, "Error: delay debe ser > 0\r\n");
    return FALSE;
  }

  delay_us = sampling_delay;

  // Configurar modo asíncrono y RX
  c1101_setPktFormat(3); // Async mode
  c1101_setRxMode();

  char buffer[100];
  sprintf(buffer, "\r\nEsperando señal para grabar RAW (delay=%d us)...\r\n",
          delay_us);
  uartWriteString(UART_USB, buffer);

  // Esperar a que llegue señal (GDO0 HIGH)
  uint32_t timeout = 0;
  while (c1101_getGDO0() == FALSE) {
    delayInaccurateUs(delay_us);
    timeout += delay_us;
    if (timeout > 3000000) { // Timeout de 3 segundos
      uartWriteString(UART_USB, "Timeout: No se detectó señal.\r\n");
      return FALSE;
    }
  }

  uartWriteString(UART_USB, "¡Señal detectada! Grabando...\r\n");

  // Grabar señal en buffer
  for (int i = 0; i < RECORDINGBUFFERSIZE; i++) {
    uint8_t receivedbyte = 0;
    for (int j = 7; j > -1; j--) {
      if (c1101_getGDO0()) {
        receivedbyte |= (1 << j);
        Chip_GPIO_SetPinState(LPC_GPIO_PORT, 5, 0, ON);
      } else {
        Chip_GPIO_SetPinState(LPC_GPIO_PORT, 5, 0, OFF);
      }
      delayInaccurateUs(delay_us);
    }
    recordingbuffer[i] = receivedbyte;
  }
  Chip_GPIO_SetPinState(LPC_GPIO_PORT, 5, 0, OFF); // apagar led

  uartWriteString(UART_USB, "\r\nGrabación RAW completa.\r\n");

  // Guardar captura en memoria RAM
  if (capture_out != NULL) {
    capture_out->freq_mhz = freq;
    capture_out->modulation_mode = modulation_type;
    capture_out->bandwidth_khz = band_width;
    capture_out->delay_us = delay_us;
    capture_out->data_length = RECORDINGBUFFERSIZE;
    capture_out->data_ptr = recordingbuffer;
  }

  // Mostrar preview
  uartWriteString(UART_USB, "Datos capturados (preview):\r\n");
  for (int i = 0; i < 96; i++) {
    print_hex_byte(recordingbuffer[i], recordingbuffer[i] != 0);
    if ((i + 1) % 48 == 0)
      uartWriteString(UART_USB, "\r\n");
  }
  uartWriteString(UART_USB, "...\r\n");

  return TRUE;
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
  sprintf(buffer, "Buffer size: %d bytes\r\n", RECORDINGBUFFERSIZE);
  uartWriteString(UART_USB, buffer);
  uartWriteString(UART_USB, "============================\r\n\r\n");
}

// Leer registros de C1101
void mostrar_registros() {
  char buffer[40];
  snprintf(buffer, sizeof(buffer), "\n=== REGISTROS DEL C1101 ===\r\n");
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
