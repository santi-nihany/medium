/*
 * CC1101 Tool - EDU-CIAA con sAPI
 */

#include "main.h"

// ! Pruebas para captura:
// Variables para manejo de microSD
static FATFS fs;                  // Sistema de archivos
static FIL fil;                   // Descriptor de archivo
static bool sd_mounted = false;   // Estado de montaje
static uint32_t file_counter = 0; // Contador de archivos

// Remapeo de pines
#define SD_CS_SCU_PORT 7
#define SD_CS_SCU_PIN 4
#define SD_CS_GPIO_PORT 3
#define SD_CS_GPIO_PIN 12
#define FSSDC_CS_PORT 3
#define FSSDC_CS_PIN 12

/* Directory for signal files */
// #define SIGNAL_DIR "SDC:/signals"
#define SIGNAL_DIR "SDC:"

/* File extension - Using .sig for signal files */
#define FILE_EXTENSION ".sig"

capture_t mi_captura;

/**
 * @brief Monta la tarjeta microSD con diagnóstico detallado
 * @return true si se montó correctamente
 */
bool MountSD(void) {
  FRESULT fr;
  DSTATUS disk_status;

  if (sd_mounted) {
    return true;
  }

  uartWriteString(UART_USB, "[SD] === Iniciando diagnóstico SD ===\r\n");

  static bool spi_init = false;
  if (!spi_init) {
    uartWriteString(UART_USB, "[SD] Configurando SPI...\r\n");
    spiConfig(SPI0);
    FSSDC_SetFastClock(400000);
    uartWriteString(UART_USB, "[SD] Clock: 400 kHz (init mode)\r\n");
    FSSDC_InitSPI();
    spi_init = true;
    uartWriteString(UART_USB, "[SD] Esperando estabilización...\r\n");
    delay(1000);
  }

  uartWriteString(UART_USB, "[SD] Verificando estado del disco...\r\n");
  disk_status = disk_initialize(0);

  char msg[80];
  sprintf(msg, "[SD] Estado del disco: 0x%02X\r\n", disk_status);
  uartWriteString(UART_USB, msg);

  if (disk_status != 0) {
    uartWriteString(UART_USB, "[!] ERROR: Disco no listo\r\n");
    return false;
  }

  // *** CAMBIO: Usar "SDC:" ***
  uartWriteString(UART_USB, "[SD] Montando filesystem...\r\n");
  fr = f_mount(&fs, "SDC:", 1);

  if (fr != FR_OK) {
    sprintf(msg, "[SD] ERROR: f_mount falló (FRESULT=%d)\r\n", fr);
    uartWriteString(UART_USB, msg);

    switch (fr) {
    case FR_NOT_READY:
      uartWriteString(UART_USB, "[!] FR_NOT_READY: SD no lista\r\n");
      break;
    case FR_DISK_ERR:
      uartWriteString(UART_USB, "[!] FR_DISK_ERR: Error de hardware\r\n");
      break;
    case FR_NO_FILESYSTEM:
      uartWriteString(
          UART_USB,
          "[!] FR_NO_FILESYSTEM: SD sin formatear (necesita FAT32)\r\n");
      break;
    case FR_INVALID_DRIVE:
      uartWriteString(UART_USB, "[!] FR_INVALID_DRIVE: Drive inválido\r\n");
      break;
    default:
      sprintf(msg, "[!] Error desconocido: %d\r\n", fr);
      uartWriteString(UART_USB, msg);
    }
    return false;
  }

  // *** CAMBIO: Usar "SDC:" ***
  uartWriteString(UART_USB, "[SD] Verificando acceso...\r\n");
  DWORD fre_clust;
  FATFS *fs_ptr;
  fr = f_getfree("SDC:", &fre_clust, &fs_ptr);

  if (fr == FR_OK) {
    sd_mounted = true;

    FSSDC_SetFastClock(14000000);
    uartWriteString(UART_USB, "[SD] Clock: 14 MHz (fast mode)\r\n");

    DWORD tot_sect = (fs_ptr->n_fatent - 2) * fs_ptr->csize;
    DWORD fre_sect = fre_clust * fs_ptr->csize;

    sprintf(msg, "[SD] Capacidad total: %lu MB\r\n",
            (unsigned long)(tot_sect / 2048));
    uartWriteString(UART_USB, msg);

    sprintf(msg, "[SD] Espacio libre: %lu MB\r\n",
            (unsigned long)(fre_sect / 2048));
    uartWriteString(UART_USB, msg);

    // uartWriteString(UART_USB, "[SD] Creando directorio signals/...\r\n");
    // fr = f_mkdir(SIGNAL_DIR);
    // if (fr == FR_OK) {
    //   uartWriteString(UART_USB, "[SD] Directorio creado\r\n");
    // } else if (fr == FR_EXIST) {
    //   uartWriteString(UART_USB, "[SD] Directorio ya existe\r\n");
    // } else {
    //   sprintf(msg, "[SD] WARN: No se pudo crear directorio (FRESULT=%d)\r\n",
    //           fr);
    //   uartWriteString(UART_USB, msg);
    // }

    uartWriteString(UART_USB, "[SD] ✓ Montada OK\r\n");
    uartWriteString(UART_USB, "[SD] =================================\r\n\r\n");
    return true;
  }

  sprintf(msg, "[SD] ERROR: No se puede acceder (FRESULT=%d)\r\n", fr);
  uartWriteString(UART_USB, msg);
  // *** CAMBIO: Usar "SDC:" ***
  f_mount(NULL, "SDC:", 0);
  sd_mounted = false;
  return false;
}

void store_capture(void) {
  uartWriteString(UART_USB, "\r\n=== ALMACENANDO CAPTURA EN SD ===\r\n");

  char buffer[100];
  char filename[64];
  FRESULT fr;
  UINT bytes_written;
  uint32_t total_bytes = 0;

  // Mostrar datos que se van a guardar
  sprintf(buffer, "Frecuencia: %.2f MHz\r\n", mi_captura.freq_mhz);
  uartWriteString(UART_USB, buffer);
  sprintf(buffer, "Modulación: %d\r\n", mi_captura.modulation_mode);
  uartWriteString(UART_USB, buffer);
  sprintf(buffer, "Bandwidth: %.2f kHz\r\n", mi_captura.bandwidth_khz);
  uartWriteString(UART_USB, buffer);
  sprintf(buffer, "Delay: %d us\r\n", mi_captura.delay_us);
  uartWriteString(UART_USB, buffer);
  sprintf(buffer, "Data length: %d bytes\r\n", mi_captura.data_length);
  uartWriteString(UART_USB, buffer);

  // Verificar que hay datos para guardar
  if (mi_captura.data_ptr == NULL || mi_captura.data_length == 0) {
    uartWriteString(UART_USB, "[!] ERROR: No hay datos para guardar\r\n");
    uartWriteString(UART_USB, "[!] Primero captura con 'recraw <delay>'\r\n");
    return;
  }

  // Montar SD si no está montada
  if (!sd_mounted) {
    if (!MountSD()) {
      uartWriteString(UART_USB, "[!] ERROR: No se pudo montar SD\r\n");
      return;
    }
  }

  // Generar nombre de archivo secuencial
  sprintf(filename, "%s/capture%lu%s", SIGNAL_DIR,
          (unsigned long)file_counter++, FILE_EXTENSION);

  sprintf(buffer, "[SD] Guardando en: %s\r\n", filename);
  uartWriteString(UART_USB, buffer);

  sprintf(buffer, "[DEBUG] Filename generado: '%s'\r\n", filename);
  uartWriteString(UART_USB, buffer);

  // Abrir archivo (crear nuevo, sobrescribir si existe)
  fr = f_open(&fil, filename, FA_WRITE | FA_CREATE_ALWAYS);
  // fr = f_open(&fil, "SDC:/abc", FA_WRITE | FA_CREATE_ALWAYS);
  // fr = f_open(&fil, "SDC:/capture.sig", FA_WRITE | FA_CREATE_ALWAYS);
  if (fr != FR_OK) {
    sprintf(buffer, "[!] ERROR: No se pudo crear archivo (%d)\r\n", fr);
    uartWriteString(UART_USB, buffer);
    return;
  }

  // ========== ESCRIBIR HEADER ASCII ==========
  char header[200];
  int header_len = sprintf(
      header,
      "CC1101_CAPTURE;VER1\r\n"
      "FREQ_MHZ=%.2f\r\n"
      "MODULATION=%d\r\n"
      "BANDWIDTH_KHZ=%.2f\r\n"
      "DELAY_US=%lu\r\n"
      "DATA_LENGTH=%u\r\n"
      "---DATA_START---\r\n",
      mi_captura.freq_mhz, mi_captura.modulation_mode, mi_captura.bandwidth_khz,
      (unsigned long)mi_captura.delay_us, mi_captura.data_length);

  fr = f_write(&fil, header, header_len, &bytes_written);
  if (fr != FR_OK || bytes_written != (UINT)header_len) {
    sprintf(buffer, "[!] ERROR: Fallo escribir header (%d)\r\n", fr);
    uartWriteString(UART_USB, buffer);
    f_close(&fil);
    return;
  }
  total_bytes += bytes_written;

  // ========== ESCRIBIR DATOS BINARIOS ==========
  fr = f_write(&fil, mi_captura.data_ptr, mi_captura.data_length,
               &bytes_written);
  if (fr != FR_OK || bytes_written != mi_captura.data_length) {
    sprintf(buffer, "[!] ERROR: Fallo escribir datos (%d)\r\n", fr);
    uartWriteString(UART_USB, buffer);
    f_close(&fil);
    return;
  }
  total_bytes += bytes_written;

  // Cerrar archivo
  fr = f_close(&fil);
  if (fr != FR_OK) {
    sprintf(buffer, "[!] ERROR: Fallo cerrar archivo (%d)\r\n", fr);
    uartWriteString(UART_USB, buffer);
    return;
  }

  // Éxito!
  sprintf(buffer, "[+] OK: %lu bytes guardados\r\n",
          (unsigned long)total_bytes);
  uartWriteString(UART_USB, buffer);
  sprintf(buffer, "[+] Archivo: %s\r\n", filename);
  uartWriteString(UART_USB, buffer);
  uartWriteString(UART_USB, "=================================\r\n\r\n");
}

void cmd_list_files(void) {
  DIR dir;
  FILINFO fno;
  FRESULT fr;
  uint32_t count = 0;

  if (!sd_mounted) {
    uartWriteString(UART_USB, "[!] SD no montada\r\n");
    return;
  }

  uartWriteString(UART_USB, "\r\n=== ARCHIVOS EN SD (RAÍZ) ===\r\n");

  // Abrir directorio raíz - usar "SDC:/" o SIGNAL_DIR
  fr = f_opendir(&dir, "SDC:/");
  if (fr != FR_OK) {
    char msg[80];
    sprintf(msg, "[!] Error abriendo raíz (FRESULT=%d)\r\n", fr);
    uartWriteString(UART_USB, msg);
    return;
  }

  while (1) {
    fr = f_readdir(&dir, &fno);
    if (fr != FR_OK || fno.fname[0] == 0)
      break;

    // Filtrar solo archivos .sig (no directorios)
    if (!(fno.fattrib & AM_DIR) && strstr(fno.fname, FILE_EXTENSION) != NULL) {
      char buffer[100];
      sprintf(buffer, "%3lu. %s (%lu bytes)\r\n", (unsigned long)++count,
              fno.fname, (unsigned long)fno.fsize);
      uartWriteString(UART_USB, buffer);
    }
  }

  f_closedir(&dir);

  if (count == 0) {
    uartWriteString(UART_USB, "(No hay archivos .sig)\r\n");
  }

  char msg[50];
  sprintf(msg, "Total: %lu archivo(s)\r\n", (unsigned long)count);
  uartWriteString(UART_USB, msg);
  uartWriteString(UART_USB, "=============================\r\n\r\n");
}

void cmd_test_sd(void) {
  uartWriteString(UART_USB, "\r\n=== TEST TARJETA SD ===\r\n");

  // Forzar reintento de montaje
  sd_mounted = false;

  if (MountSD()) {
    uartWriteString(UART_USB, "[+] SD operativa\r\n");
  } else {
    uartWriteString(UART_USB, "[!] SD no funciona\r\n");
  }
}

void mostrar_ayuda(void) {
  uartWriteString(UART_USB, "\r\n\n\n\n=== COMANDOS DISPONIBLES ===\r\n");
  uartWriteString(UART_USB, "help         - Muestra esta ayuda\r\n");
  uartWriteString(UART_USB, "reset_c1101  - Resetea el CC1101\r\n");
  uartWriteString(UART_USB, "config       - Muestra configuración actual\r\n");
  uartWriteString(UART_USB,
                  "store        - Almacena captura en memoria microSD\r\n");
  uartWriteString(UART_USB,
                  "list         - Lista archivos en memoria microSD\r\n");
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

  } else if (strcmp(command, "reset_c1101") == 0) {
    shield_c1101Setup();
    uartWriteString(UART_USB, "CC1101 reseteado.\r\n");
    mostrar_registros();

  } else if (strcmp(command, "config") == 0) {
    mostrar_config();
    mostrar_registros();

  } else if (strcmp(command, "testsd") == 0) {
    cmd_test_sd();

  } else if (strcmp(command, "store") == 0) {
    store_capture();

  } else if (strcmp(command, "list") == 0) {
    cmd_list_files();

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
        uartWriteString(UART_USB, "\r\n");
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
  c1101_initCustomGPIO(); // cambiar nombre a shield_c1101InitCustomGPIO igual
                          // que con el resto de funciones que pegue en shield.c
  spiConfig(SPI0);

  // remapeo de pines para microSD (Chip Select)
  Chip_SCU_PinMuxSet(SD_CS_SCU_PORT, SD_CS_SCU_PIN,
                     (SCU_MODE_PULLUP | SCU_MODE_FUNC0));
  Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, SD_CS_GPIO_PORT, SD_CS_GPIO_PIN);
  Chip_GPIO_SetPinOutHigh(LPC_GPIO_PORT, SD_CS_GPIO_PORT, SD_CS_GPIO_PIN);

  // uartWriteString(UART_USB, "\r\n");
  // MountSD(); // Intenta montar, si falla se reintentará en store_capture()
  // uartWriteString(UART_USB, "\r\n");

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
