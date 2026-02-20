//===----------------------------------------------------------------------===//
///
/// \file
/// Task de interfaz de usuario.
///
//===----------------------------------------------------------------------===//

#include "tasks/task_ui.h"
#include "FreeRTOS.h"
#include "drivers/buttons.h"
#include "modules/display.h"
#include "modules/ir.h"
#include "modules/rf.h"
#include "modules/storage.h"
#include "task.h"
#include "utils/sig.h"
#include "utils/sprites.h"

/// Stack de la task UI (en palabras).
#define UI_TASK_STACK_WORDS 512
/// Prioridad de la task UI.
#define UI_TASK_PRIORITY (tskIDLE_PRIORITY + 2)

typedef enum {
  UI_MODE_IR = 0,
  UI_MODE_RF = 1,
} uiMode_t;

/// Dibuja pantalla principal.
static void uiDrawMain(uiMode_t mode) {
  displayDrawRectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_BLACK,
                       TRUE);
  displayPlace(sprite_background, 0, 0);
  displayPlace(sprite_title, 36, 4);
  displayPlace(sprite_ir, 26, 22);
  displayPlace(sprite_rf, 71, 22);

  if (mode == UI_MODE_IR) {
    displayPlace(sprite_lselector, 19, 32);
    displayPlace(sprite_rselector, 59, 32);
  } else {
    displayPlace(sprite_lselector, 64, 32);
    displayPlace(sprite_rselector, 104, 32);
  }

  displayUpdate();
}

/// Dibuja pantalla de captura en curso.
static void uiDrawCapturing(void) {
  displayDrawRectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_BLACK,
                       TRUE);
  displayText("Capturando...", 25, 28, FALSE);
  displayUpdate();
}

/// Dibuja pantalla cuando no hay microSD.
static void uiDrawNeedSd(void) {
  displayPlace(sprite_background, 0, 0);
  displayText("Coloca la microSD", 14, 28, FALSE);
  displayUpdate();
}

/// Callback para cancelar captura desde UI.
static bool_t uiCaptureCancelCallback(void *context) {
  (void)context;
  storageUpdate();
  if (!storageIsReady()) {
    return TRUE;
  }
  return swBackRead();
}

/// Guarda la última captura IR en archivo .sig.
static bool_t uiSaveLastIrCaptureToSig(void) {
  const IRPulse *pulses = NULL;
  uint16_t pulseCount = 0;
  uint32_t edges[IR_MAX_PULSES];
  sigRecord_t record;
  uint8_t metadata[16];
  uint32_t metadataSize = 0;
  uint8_t address = 0;
  uint8_t command = 0;
  bool_t hasNec = FALSE;

  if (!irGetLastCapture(&pulses, &pulseCount) || pulses == NULL ||
      pulseCount == 0) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] WARN: no hay captura IR valida para guardar\r\n");
#endif
    return FALSE;
  }

  for (uint16_t i = 0; i < pulseCount; i++) {
    edges[i] = pulses[i].durationUs;
  }

  hasNec = irDecodeLastNec(&address, &command);
  if (hasNec) {
    if (!sigMetadataAppendTlv(metadata, sizeof(metadata), &metadataSize,
                              SIG_META_NEC_ADDR, &address, sizeof(address)) ||
        !sigMetadataAppendTlv(metadata, sizeof(metadata), &metadataSize,
                              SIG_META_NEC_CMD, &command, sizeof(command))) {
#ifdef MEDIUM_DEBUG
      printf("[tasks  ] [ui] WARN: no se pudo serializar metadata NEC\r\n");
#endif
      hasNec = FALSE;
      metadataSize = 0;
    }
  }

  memset(&record, 0, sizeof(record));
  record.signalType = SIG_SIGNAL_TYPE_IR;
  record.flags = (pulses[0].level ? SIG_FLAG_START_LEVEL : 0U);
  record.tickScale = -6; // 1us por tick
  record.edgeCount = pulseCount;
  record.edges = edges;
  record.metadata = hasNec ? metadata : NULL;
  record.metadataSize = metadataSize;
  if (hasNec) {
    record.flags |= SIG_FLAG_HAS_METADATA;
  }

  if (storageSigSave("IR001.sig", &record)) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] IR guardada en IR001.sig (%u edges)\r\n",
           pulseCount);
    if (hasNec) {
      printf("[tasks  ] [ui] Metadata NEC addr=0x%02X cmd=0x%02X\r\n", address,
             command);
    }
#endif
    return TRUE;
  }

#ifdef MEDIUM_DEBUG
  printf("[tasks  ] [ui] ERROR: no se pudo guardar IR001.sig\r\n");
#endif
  return FALSE;
}

/// Guarda la última captura RF en archivo .sig.
static bool_t uiSaveLastRfCaptureToSig(void) {
  const uint16_t *pulses = NULL;
  uint16_t pulseCount = 0;
  bool_t firstLevel = FALSE;
  uint32_t edges[RF_CAPTURE_PULSES_MAX];
  sigRecord_t record;

  if (!rfGetLastCapture(&pulses, &pulseCount, &firstLevel) || pulses == NULL ||
      pulseCount == 0) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] WARN: no hay captura RF valida para guardar\r\n");
#endif
    return FALSE;
  }

  for (uint16_t i = 0; i < pulseCount; i++) {
    edges[i] = pulses[i];
  }

  memset(&record, 0, sizeof(record));
  record.signalType = SIG_SIGNAL_TYPE_RF;
  record.flags = firstLevel ? SIG_FLAG_START_LEVEL : 0U;
  record.tickScale = -6; // 1us por tick
  record.edgeCount = pulseCount;
  record.edges = edges;

  if (storageSigSave("RF001.sig", &record)) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] RF guardada en RF001.sig (%u edges)\r\n",
           pulseCount);
#endif
    return TRUE;
  }

#ifdef MEDIUM_DEBUG
  printf("[tasks  ] [ui] ERROR: no se pudo guardar RF001.sig\r\n");
#endif
  return FALSE;
}

/// Task de interacción de usuario.
static void uiTask(void *taskParam) {
  uiMode_t mode = UI_MODE_IR;
  int8_t lastJoystickX = 0;
  bool_t lastEnter = FALSE;
  bool_t lastBack = FALSE;
  bool_t showingNeedSd = FALSE;

  (void)taskParam;
  uiDrawMain(mode);

  while (1) {
    bool_t enterNow;
    bool_t backNow;

    storageUpdate();

    if (!storageIsReady()) {
      if (!showingNeedSd) {
        showingNeedSd = TRUE;
        uiDrawNeedSd();
#ifdef MEDIUM_DEBUG
        printf("[tasks  ] [ui] SD no disponible\r\n");
#endif
      }
      lastEnter = swEnterRead();
      lastBack = swBackRead();
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (showingNeedSd) {
      showingNeedSd = FALSE;
      uiDrawMain(mode);
    }

    {
      JoystickState joystick = joystickRead();
      if (joystick.x != lastJoystickX) {
        if (joystick.x == -1) {
          mode = UI_MODE_IR;
          uiDrawMain(mode);
        } else if (joystick.x == 1) {
          mode = UI_MODE_RF;
          uiDrawMain(mode);
        }
        lastJoystickX = joystick.x;
      }
    }

    enterNow = swEnterRead();
    backNow = swBackRead();

    if (enterNow && !lastEnter) {
      bool_t captureOk = FALSE;
      bool_t saveOk = FALSE;

      uiDrawCapturing();

      if (mode == UI_MODE_IR) {
        captureOk = irRecordWithCancel(uiCaptureCancelCallback, NULL);
        if (captureOk && storageIsReady()) {
          saveOk = uiSaveLastIrCaptureToSig();
        }
      } else {
        captureOk = rfCapture433MHzWithCancel(uiCaptureCancelCallback, NULL);
        if (captureOk && storageIsReady()) {
          saveOk = uiSaveLastRfCaptureToSig();
        }
      }

#ifdef MEDIUM_DEBUG
      if (!captureOk) {
        if (storageIsReady()) {
          printf("[tasks  ] [ui] Captura cancelada/timeout\r\n");
        } else {
          printf("[tasks  ] [ui] Captura cancelada por retiro de SD\r\n");
        }
      } else if (!saveOk) {
        printf("[tasks  ] [ui] Captura OK pero guardado FAIL\r\n");
      }
#endif
      if (storageIsReady()) {
        uiDrawMain(mode);
      } else {
        uiDrawNeedSd();
        showingNeedSd = TRUE;
      }
    }

    if (backNow && !lastBack) {
#ifdef MEDIUM_DEBUG
      printf("[tasks  ] [ui] SW_BACK presionado\r\n");
#endif
    }

    lastEnter = enterNow;
    lastBack = backNow;
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

/// Crea la task de UI.
bool_t taskUiCreate(void) {
  return (xTaskCreate(uiTask, "ui", UI_TASK_STACK_WORDS, NULL, UI_TASK_PRIORITY,
                      NULL) == pdPASS)
             ? TRUE
             : FALSE;
}
