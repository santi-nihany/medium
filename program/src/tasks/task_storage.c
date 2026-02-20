//===----------------------------------------------------------------------===//
///
/// \file
/// Task de monitoreo de almacenamiento.
///
//===----------------------------------------------------------------------===//

#include "tasks/task_storage.h"
#include "FreeRTOS.h"
#include "modules/storage.h"
#include "task.h"

/// Stack de la task de storage (en palabras).
#define STORAGE_TASK_STACK_WORDS 256
/// Prioridad de la task de storage.
#define STORAGE_TASK_PRIORITY (tskIDLE_PRIORITY + 3)
/// Período de monitoreo de la microSD.
#define STORAGE_TASK_PERIOD_MS 20
/// Período del probe de filesystem (sin pin CD).
#define STORAGE_PROBE_PERIOD_MS 100

/// Task periódica de actualización de estado de microSD.
static void taskStorage(void *taskParam) {
  bool_t lastReady = FALSE;
  TickType_t lastProbeTick = 0;

  (void)taskParam;
  lastProbeTick = xTaskGetTickCount();

  while (1) {
    storageUpdate();

    if (storageIsReady()) {
      TickType_t now = xTaskGetTickCount();
      if ((now - lastProbeTick) >= pdMS_TO_TICKS(STORAGE_PROBE_PERIOD_MS)) {
        (void)storageProbe();
        lastProbeTick = now;
      }
    }

    {
      bool_t ready = storageIsReady();
      if (ready != lastReady) {
#ifdef MEDIUM_DEBUG
        printf("[tasks  ] [storage] SD %s\r\n",
               ready ? "lista" : "no disponible");
#endif
        lastReady = ready;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(STORAGE_TASK_PERIOD_MS));
  }
}

/// Crea la task de monitoreo de almacenamiento.
bool_t taskStorageCreate(void) {
  return (xTaskCreate(taskStorage, "storage", STORAGE_TASK_STACK_WORDS, NULL,
                      STORAGE_TASK_PRIORITY, NULL) == pdPASS)
             ? TRUE
             : FALSE;
}
