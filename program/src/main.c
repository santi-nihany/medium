/*
 * CC1101 Tool - EDU-CIAA con sAPI
 */

#include "main.h"

// ! Pruebas para captura:

capture_t mi_captura;

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
  uartWriteString(UART_USB, "============================\r\n\r\n");
}

void procesar_comando(char *cmdline) {
  char *command = strtok(cmdline, " ");
  if (command == NULL)
    return;

  if (strcmp(command, "help") == 0) {
    mostrar_ayuda();

  } else if (strcmp(command, "config") == 0) {
    mostrar_config();
    mostrar_registros();

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
      if (cmd_recraw(d, &mi_captura)) {
        uartWriteString(UART_USB,
                        "[+] Captura guardada en estructura mi_captura\r\n");
        uartWriteString(UART_USB, "=== DATOS DE LA CAPTURA ===\r\n");
        char buffer[100];
        sprintf(buffer, "Frecuencia: %.2f MHz\r\n", mi_captura.freq_mhz);
        uartWriteString(UART_USB, buffer);
        uartWriteString(UART_USB, "Modulación: ");
        sprintf(buffer, "%d", mi_captura.modulation_mode);
        uartWriteString(UART_USB, buffer);
        sprintf(buffer, "Bandwidth: %.2f kHz\r\n", mi_captura.bandwidth_khz);
        uartWriteString(UART_USB, buffer);
        sprintf(buffer, "Delay: %d us\r\n", mi_captura.delay_us);
        uartWriteString(UART_USB, buffer);
        sprintf(buffer, "Data length: %d bytes\r\n", mi_captura.data_length);
        uartWriteString(UART_USB, buffer);
        uartWriteString(UART_USB, "Data pointer: ");
        sprintf(buffer, "%p\r\n", mi_captura.data_ptr);
        uartWriteString(UART_USB, buffer);
        uartWriteString(UART_USB, "=== FIN DE LA CAPTURA ===\r\n");
      } else {
        uartWriteString(UART_USB, "[!] Error durante captura\r\n");
      }
    } else {
      uartWriteString(UART_USB, "Error: falta parámetro (ej: recraw 10)\r\n");
    }
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

  shield_c1101Setup();

  uartWriteString(UART_USB, "Setup completado.\r\n");
  mostrar_config();
  mostrar_registros();
  mostrar_ayuda();

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
      } else if (length < BUF_LENGTH - 1) {
        buffer[length++] = data;
        uartWriteByte(UART_USB, data);
      }
    }
  }

  return 0;
}
