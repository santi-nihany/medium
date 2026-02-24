//===----------------------------------------------------------------------===//
///
/// \file
/// Inicio del programa. Contiene llamados a inicialización y bucle principal.
///
//===----------------------------------------------------------------------===//

#include "main.h"
#include "FreeRTOS.h"
#include "drivers/buttons.h"
#include "modules/display.h"
#include "modules/ir.h"
#include "modules/rf.h"
#include "modules/spi_bus.h"
#include "modules/storage.h"
#include "task.h"
#include "tasks/task_storage.h"
#include "tasks/task_ui.h"

int main(void) {
  // Inicializar placa, puertos y protocolos
  boardConfig();
  adcConfig(ADC_ENABLE);   // Usado por buttons.c
  i2cConfig(I2C0, 100000); // Usado por sh1106.c
  spiConfig(SPI0);         // Usado por cc1101.c y storage.c
  spiBusInit();            // Mutex para arbitrar SPI entre SD y CC1101

#ifdef MEDIUM_DEBUG
  // Configurar UART para debug
  uartConfig(UART_USB, 115200);
  printf("\r\n\r\n[       ] [main] Placa inicializada\r\n");
#endif

  // Inicializar componentes
  delayInaccurateMs(250); // Esperar a estabilización
  buttonsInit();
  displayInit();
  storageInit();
  irInit();
  rfInit();

#ifdef MEDIUM_DEBUG
  printf("[       ] [main] Componentes inicializados\r\n");
#endif

  if (!taskStorageCreate()) {
#ifdef MEDIUM_DEBUG
    printf("[       ] [main] ERROR: no se pudo crear task Storage\r\n");
#endif
    while (1) {
      delay(500);
    }
  }

  if (!taskUiCreate()) {
#ifdef MEDIUM_DEBUG
    printf("[       ] [main] ERROR: no se pudo crear task UI\r\n");
#endif
    while (1) {
      delay(500);
    }
  }

  vTaskStartScheduler();

  while (1) {
  }
}
