//===----------------------------------------------------------------------===//
///
/// \file
/// Manejo de almacenamiento en la microSD.
///
//===----------------------------------------------------------------------===//

#include "modules/storage.h"
#include "chip.h"
#include "diskio.h"
#include "ff.h"
#include "fssdc.h"
#include "modules/spi_bus.h"
#include "sapi_sdcard.h"

/// Timeout máximo para preparar la microSD.
#define STORAGE_SD_READY_TIMEOUT_MS 3000
/// Tamaño máximo de path normalizado para FatFs.
#define STORAGE_PATH_MAX_LEN 128
/// Cantidad de reinicios de init SPI ante error.
#define STORAGE_SD_RECOVERY_RETRIES 3
/// Período de reintento de init SPI sin pin CD (en llamadas a storageUpdate).
#define STORAGE_REINIT_PERIOD_UPDATES 50U
/// Reintentar init al acumular fallos de mount consecutivos.
#define STORAGE_MOUNT_FAIL_REINIT_THRESHOLD 5U

static sdcard_t storageSdCard;
static bool_t storageSdReady = FALSE;
static bool_t storageReinitPending = FALSE;

/// Toma mutex SPI compartido entre SD y CC1101.
static bool_t storageSpiLock(void) { return spiBusLock(portMAX_DELAY); }

/// Libera mutex SPI compartido entre SD y CC1101.
static void storageSpiUnlock(void) { spiBusUnlock(); }

/// Contexto de callbacks de lectura/escritura sobre FatFs.
typedef struct {
  FIL *file;
} storageFatFsContext_t;

/// Inicializa el driver SD una única vez.
static bool_t storageInitDriverIfNeeded(void) {
  bool_t initOk = TRUE;

  if (!storageSdReady) {
    // FSSDC requiere CS ya muxeado y configurado como salida antes de InitSPI.
    Chip_GPIO_SetPinDIROutput(LPC_GPIO_PORT, FSSDC_CS_PORT, FSSDC_CS_PIN);
    Chip_GPIO_SetPinOutHigh(LPC_GPIO_PORT, FSSDC_CS_PORT, FSSDC_CS_PIN);

    if (storageSpiLock()) {
      initOk = sdcardInit(&storageSdCard);
      storageSpiUnlock();
    } else {
      initOk = FALSE;
    }
    storageSdReady = TRUE;
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] sdcardInit solicitado\r\n");
    printf("[modules] [storage] SD CS configurado en P%u_%u (HIGH)\r\n",
           (unsigned)FSSDC_CS_PORT, (unsigned)FSSDC_CS_PIN);
    if (!initOk) {
      printf("[modules] [storage] WARN: sdcardInit devolvio FALSE\r\n");
    }
#endif
  }

  return initOk;
}

/// Texto para estado de la SD.
static const char *storageStatusName(sdcardStatus_t status) {
  switch (status) {
  case SDCARD_Status_Removed:
    return "Removed";
  case SDCARD_Status_Inserted:
    return "Inserted";
  case SDCARD_Status_NativeMode:
    return "NativeMode";
  case SDCARD_Status_Initializing:
    return "Initializing";
  case SDCARD_Status_ReadyUnmounted:
    return "ReadyUnmounted";
  case SDCARD_Status_ReadyMounted:
    return "ReadyMounted";
  case SDCARD_Status_Error:
  default:
    return "Error";
  }
}

/// Copia path al destino agregando prefijo "SDC:/" cuando corresponde.
static bool_t storageNormalizePath(const char *path, char *out,
                                   uint32_t outSize) {
  uint32_t len;

  if (path == NULL || out == NULL || outSize < 6U) {
    return FALSE;
  }

  len = (uint32_t)strlen(path);
  if (len == 0U || len >= outSize) {
    return FALSE;
  }

  if (strchr(path, ':') != NULL) {
    memcpy(out, path, len + 1U);
    return TRUE;
  }

  if ((len + 5U) >= outSize) {
    return FALSE;
  }

  memcpy(out, "SDC:/", 5U);
  memcpy(&out[5], path, len + 1U);
  return TRUE;
}

/// Callback genérico de escritura hacia FatFs.
static bool_t storageFatFsWrite(void *context, const uint8_t *data,
                                uint32_t size) {
  storageFatFsContext_t *ctx = (storageFatFsContext_t *)context;
  UINT written = 0U;
  FRESULT result;

  if (ctx == NULL || ctx->file == NULL || data == NULL) {
    return FALSE;
  }

  if (!storageSpiLock()) {
    return FALSE;
  }
  result = f_write(ctx->file, data, (UINT)size, &written);
  storageSpiUnlock();
  return (result == FR_OK && written == (UINT)size) ? TRUE : FALSE;
}

/// Callback genérico de lectura desde FatFs.
static bool_t storageFatFsRead(void *context, uint8_t *data, uint32_t size) {
  storageFatFsContext_t *ctx = (storageFatFsContext_t *)context;
  UINT read = 0U;
  FRESULT result;

  if (ctx == NULL || ctx->file == NULL || data == NULL) {
    return FALSE;
  }

  if (!storageSpiLock()) {
    return FALSE;
  }
  result = f_read(ctx->file, data, (UINT)size, &read);
  storageSpiUnlock();
  return (result == FR_OK && read == (UINT)size) ? TRUE : FALSE;
}

/// Espera hasta que la SD quede montada para I/O de archivos.
static bool_t storageEnsureReady(void) {
  sdcardStatus_t lastStatus = SDCARD_Status_Error;
  uint8_t recoveries = 0U;

  if (!storageInitDriverIfNeeded()) {
    return FALSE;
  }

  for (uint32_t t = 0; t < STORAGE_SD_READY_TIMEOUT_MS; t++) {
    sdcardStatus_t status;

    if (!storageSpiLock()) {
      delay(1);
      continue;
    }
    sdcardUpdate();
    status = sdcardStatus();
    storageSpiUnlock();

#ifdef MEDIUM_DEBUG
    if (status != lastStatus) {
      printf("[modules] [storage] SD status -> %s\r\n",
             storageStatusName(status));
      lastStatus = status;
    }
#endif

    if (status == SDCARD_Status_ReadyMounted) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [storage] SD lista y montada\r\n");
#endif
      return TRUE;
    }
    if (status == SDCARD_Status_ReadyUnmounted) {
      bool_t mounted = FALSE;
      if (storageSpiLock()) {
        mounted = sdcardMount(TRUE);
        storageSpiUnlock();
      }
      if (mounted) {
#ifdef MEDIUM_DEBUG
        printf("[modules] [storage] SD montada OK\r\n");
#endif
        return TRUE;
      }
    }

    if (status == SDCARD_Status_Error &&
        recoveries < STORAGE_SD_RECOVERY_RETRIES) {
      recoveries++;
#ifdef MEDIUM_DEBUG
      printf("[modules] [storage] WARN: SD en error, reintentando init SPI "
             "(%u/%u)\r\n",
             (unsigned)recoveries, (unsigned)STORAGE_SD_RECOVERY_RETRIES);
#endif
      if (storageSpiLock()) {
        FSSDC_InitSPI();
        storageSpiUnlock();
      }
      delay(50);
    }

    delay(1);
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [storage] ERROR: timeout preparando SD (status=%u)\r\n",
         (unsigned)sdcardStatus());
#endif
  return (sdcardStatus() == SDCARD_Status_ReadyMounted) ? TRUE : FALSE;
}

/// Inicializa almacenamiento SD.
bool_t storageInit(void) {
  bool_t ok = storageInitDriverIfNeeded();

#ifdef MEDIUM_DEBUG
  printf("[modules] [storage] init %s (no bloqueante)\r\n", ok ? "OK" : "FAIL");
#endif
  return ok;
}

/// Actualiza estado de la SD.
void storageUpdate(void) {
  static uint32_t updateCounter = 0U;
  static uint8_t mountFailStreak = 0U;

  if (storageSdReady) {
    sdcardStatus_t status;

    if (!storageSpiLock()) {
      return;
    }
    sdcardUpdate();
    status = sdcardStatus();
    storageSpiUnlock();
    updateCounter++;

    // Sin automount, intentar montar cuando el driver quedó listo.
    if (status == SDCARD_Status_ReadyUnmounted) {
      bool_t mounted = FALSE;
      if (storageSpiLock()) {
        mounted = sdcardMount(TRUE);
        storageSpiUnlock();
      }
      if (mounted) {
        mountFailStreak = 0U;
        storageReinitPending = FALSE;
#ifdef MEDIUM_DEBUG
        printf("[modules] [storage] SD montada desde storageUpdate\r\n");
#endif
      } else {
        mountFailStreak++;
        if (storageReinitPending ||
            mountFailStreak >= STORAGE_MOUNT_FAIL_REINIT_THRESHOLD) {
          mountFailStreak = 0U;
#ifdef MEDIUM_DEBUG
          printf(
              "[modules] [storage] WARN: mount falla repetido, reinit SPI\r\n");
#endif
          if (storageSpiLock()) {
            FSSDC_InitSPI();
            storageSpiUnlock();
          }
        }
      }
      return;
    }

    // Sin pin CD: reintentar init SPI periódicamente para detectar inserción.
    if ((storageReinitPending || status == SDCARD_Status_Error ||
         status == SDCARD_Status_Removed) &&
        (updateCounter % STORAGE_REINIT_PERIOD_UPDATES) == 0U) {
#ifdef MEDIUM_DEBUG
      printf("[modules] [storage] WARN: reinit SPI periódico\r\n");
#endif
      if (storageSpiLock()) {
        FSSDC_InitSPI();
        storageSpiUnlock();
      }
    }
  }
}

/// Informa si la microSD está lista y montada.
bool_t storageIsReady(void) {
  if (!storageSdReady) {
    return FALSE;
  }
  return (sdcardStatus() == SDCARD_Status_ReadyMounted) ? TRUE : FALSE;
}

/// Ejecuta un probe liviano de filesystem para detectar extracción sin pin CD.
bool_t storageProbe(void) {
  DWORD freeClusters = 0;
  DWORD sectorCount = 0;
  FATFS *fs = NULL;
  FRESULT result;
  DRESULT diskResult;

  if (!storageIsReady()) {
    return FALSE;
  }

  // Probe de bajo nivel: fuerza transacción SPI real contra la tarjeta.
  if (!storageSpiLock()) {
    return FALSE;
  }
  diskResult = disk_ioctl(0, GET_SECTOR_COUNT, &sectorCount);
  storageSpiUnlock();
  if (diskResult != RES_OK || sectorCount == 0U) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] WARN: probe SD fallo (disk_ioctl=%u), "
           "desmontando\r\n",
           (unsigned)diskResult);
#endif
    if (storageSpiLock()) {
      (void)sdcardMount(FALSE);
      storageSpiUnlock();
    }
    storageReinitPending = TRUE;
    return FALSE;
  }

  // Probe de filesystem: valida volumen FAT montado.
  if (!storageSpiLock()) {
    return FALSE;
  }
  result = f_getfree("SDC:", &freeClusters, &fs);
  storageSpiUnlock();
  if (result == FR_OK) {
    return TRUE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [storage] WARN: probe SD fallo (f_getfree=%u), "
         "desmontando\r\n",
         (unsigned)result);
#endif
  if (storageSpiLock()) {
    (void)sdcardMount(FALSE);
    storageSpiUnlock();
  }
  storageReinitPending = TRUE;
  return FALSE;
}

/// Verifica si un archivo existe en microSD.
bool_t storageFileExists(const char *path) {
  char fullPath[STORAGE_PATH_MAX_LEN];
  FILINFO info;
  FRESULT result;

  if (path == NULL) {
    return FALSE;
  }
  if (!storageEnsureReady()) {
    return FALSE;
  }
  if (!storageNormalizePath(path, fullPath, sizeof(fullPath))) {
    return FALSE;
  }

  if (!storageSpiLock()) {
    return FALSE;
  }
  result = f_stat(fullPath, &info);
  storageSpiUnlock();
  return (result == FR_OK) ? TRUE : FALSE;
}

/// Elimina un archivo de microSD.
bool_t storageFileDelete(const char *path) {
  char fullPath[STORAGE_PATH_MAX_LEN];
  FRESULT result;

  if (path == NULL) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] ERROR: delete con path NULL\r\n");
#endif
    return FALSE;
  }
  if (!storageEnsureReady()) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] ERROR: delete sin SD lista\r\n");
#endif
    return FALSE;
  }
  if (!storageNormalizePath(path, fullPath, sizeof(fullPath))) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] ERROR: path invalido para delete: %s\r\n", path);
#endif
    return FALSE;
  }

  if (!storageSpiLock()) {
    return FALSE;
  }
  result = f_unlink(fullPath);
  storageSpiUnlock();
  if (result == FR_OK) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] Delete OK: %s\r\n", fullPath);
#endif
    return TRUE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [storage] ERROR: f_unlink(%s)=%u\r\n", fullPath,
         (unsigned)result);
#endif
  return FALSE;
}

/// Guarda un .sig en microSD mediante FatFs.
bool_t storageSigSave(const char *path, const sigRecord_t *record) {
  char fullPath[STORAGE_PATH_MAX_LEN];
  FIL file;
  FRESULT result;
  storageFatFsContext_t context;
  bool_t ok;

  if (path == NULL || record == NULL) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] ERROR: save con parametros invalidos\r\n");
#endif
    return FALSE;
  }
  if (!storageEnsureReady()) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] ERROR: save sin SD lista\r\n");
#endif
    return FALSE;
  }
  if (!storageNormalizePath(path, fullPath, sizeof(fullPath))) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] ERROR: path invalido para save: %s\r\n", path);
#endif
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [storage] Guardando .sig en %s\r\n", fullPath);
#endif
  if (!storageSpiLock()) {
    return FALSE;
  }
  result = f_open(&file, fullPath, FA_WRITE | FA_CREATE_ALWAYS);
  storageSpiUnlock();
  if (result != FR_OK) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] ERROR: f_open(save)=%u\r\n", (unsigned)result);
#endif
    return FALSE;
  }

  context.file = &file;
  ok = sigWriteRecord(storageFatFsWrite, &context, record);
  if (ok) {
    if (!storageSpiLock()) {
      ok = FALSE;
    } else {
    FRESULT syncResult = f_sync(&file);
      storageSpiUnlock();
    ok = (syncResult == FR_OK) ? TRUE : FALSE;
#ifdef MEDIUM_DEBUG
    if (!ok) {
      printf("[modules] [storage] ERROR: f_sync=%u\r\n", (unsigned)syncResult);
    }
#endif
    }
  }

  if (storageSpiLock()) {
    f_close(&file);
    storageSpiUnlock();
  }
#ifdef MEDIUM_DEBUG
  printf("[modules] [storage] Save %s (%lu edges, meta=%lu)\r\n",
         ok ? "OK" : "FAIL", (unsigned long)record->edgeCount,
         (unsigned long)record->metadataSize);
#endif
  return ok;
}

/// Lee un .sig desde microSD mediante FatFs.
bool_t storageSigLoad(const char *path, sigRecordBuffer_t *record) {
  char fullPath[STORAGE_PATH_MAX_LEN];
  FIL file;
  FRESULT result;
  storageFatFsContext_t context;
  bool_t ok;

  if (path == NULL || record == NULL) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] ERROR: load con parametros invalidos\r\n");
#endif
    return FALSE;
  }
  if (!storageEnsureReady()) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] ERROR: load sin SD lista\r\n");
#endif
    return FALSE;
  }
  if (!storageNormalizePath(path, fullPath, sizeof(fullPath))) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] ERROR: path invalido para load: %s\r\n", path);
#endif
    return FALSE;
  }

#ifdef MEDIUM_DEBUG
  printf("[modules] [storage] Cargando .sig desde %s\r\n", fullPath);
#endif
  if (!storageSpiLock()) {
    return FALSE;
  }
  result = f_open(&file, fullPath, FA_READ);
  storageSpiUnlock();
  if (result != FR_OK) {
#ifdef MEDIUM_DEBUG
    printf("[modules] [storage] ERROR: f_open(load)=%u\r\n", (unsigned)result);
#endif
    return FALSE;
  }

  context.file = &file;
  ok = sigReadRecord(storageFatFsRead, &context, record);

  if (storageSpiLock()) {
    f_close(&file);
    storageSpiUnlock();
  }
#ifdef MEDIUM_DEBUG
  if (ok) {
    printf("[modules] [storage] Load OK (type=%u edges=%lu meta=%lu "
           "tick_scale=%d)\r\n",
           (unsigned)record->signalType, (unsigned long)record->edgeCount,
           (unsigned long)record->metadataSize, (int)record->tickScale);
  } else {
    printf("[modules] [storage] Load FAIL (parse/CRC/capacidad)\r\n");
  }
#endif
  return ok;
}
