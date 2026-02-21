//===----------------------------------------------------------------------===//
///
/// \file
/// Mutex global para arbitrar acceso al bus SPI compartido.
///
//===----------------------------------------------------------------------===//

#include "modules/spi_bus.h"
#include "semphr.h"
#include "task.h"

static StaticSemaphore_t spiBusMutexBuffer;
static SemaphoreHandle_t spiBusMutex = NULL;
static bool_t spiBusInitialized = FALSE;

void spiBusInit(void) {
  if (spiBusInitialized) {
    return;
  }

  spiBusMutex = xSemaphoreCreateMutexStatic(&spiBusMutexBuffer);
  spiBusInitialized = (spiBusMutex != NULL) ? TRUE : FALSE;
}

bool_t spiBusLock(TickType_t timeoutTicks) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
    return TRUE;
  }

  if (!spiBusInitialized) {
    spiBusInit();
  }
  if (spiBusMutex == NULL) {
    return FALSE;
  }

  return (xSemaphoreTake(spiBusMutex, timeoutTicks) == pdTRUE) ? TRUE : FALSE;
}

void spiBusUnlock(void) {
  if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
    return;
  }
  if (!spiBusInitialized || spiBusMutex == NULL) {
    return;
  }

  (void)xSemaphoreGive(spiBusMutex);
}
