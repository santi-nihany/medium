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
#define UI_TASK_STACK_WORDS 640
/// Prioridad de la task UI.
#define UI_TASK_PRIORITY (tskIDLE_PRIORITY + 2)
/// Cantidad de slots por modo.
#define UI_SLOT_COUNT 5U
/// Tamaño máximo de path para nombre de slot.
#define UI_SLOT_PATH_MAX 16U
/// Capacidad máxima de edges al cargar un .sig para replay.
#define UI_SIG_EDGE_BUFFER_MAX 1024U
/// Capacidad máxima de metadata al cargar un .sig para replay.
#define UI_SIG_META_BUFFER_MAX 64U

typedef enum {
  UI_MODE_IR = 0,
  UI_MODE_RF = 1,
} uiMode_t;

typedef enum {
  UI_SCREEN_MAIN = 0,
  UI_SCREEN_SLOT_SELECT = 1,
} uiScreen_t;

typedef enum {
  UI_SLOT_ACTION_PLAY = 0,
  UI_SLOT_ACTION_DELETE = 1,
  UI_SLOT_ACTION_RECORD = 2,
} uiSlotAction_t;

/// Buffers estáticos para load/replay de .sig.
static uint32_t uiSigEdges[UI_SIG_EDGE_BUFFER_MAX];
static uint8_t uiSigMetadata[UI_SIG_META_BUFFER_MAX];

/// Dibuja pantalla principal.
static void uiDrawMain(uiMode_t mode) {
  displayPlace(sprite_background, 0, 0, DISPLAY_WHITE);
  displayPlace(sprite_title, 36, 4, DISPLAY_WHITE);
  displayPlace(sprite_ir, 26, 22, DISPLAY_WHITE);
  displayPlace(sprite_rf, 71, 22, DISPLAY_WHITE);

  if (mode == UI_MODE_IR) {
    displayPlace(sprite_lselector, 19, 32, DISPLAY_WHITE);
    displayPlace(sprite_rselector, 59, 32, DISPLAY_WHITE);
  } else {
    displayPlace(sprite_lselector, 64, 32, DISPLAY_WHITE);
    displayPlace(sprite_rselector, 104, 32, DISPLAY_WHITE);
  }

  displayUpdate();
}

/// Dibuja un botón de acción en el selector de slots.
static void uiDrawActionButton(uint8_t x, uint8_t y, const Sprite *icon,
                               bool_t enabled, bool_t selected) {
  DisplayColor iconColor = DISPLAY_WHITE;

  if (selected) {
    iconColor = DISPLAY_BLACK;
  } else {
    iconColor = enabled ? DISPLAY_WHITE : DISPLAY_BLACK;
  }
  displayPlace(*icon, (uint8_t)(x + 1), (uint8_t)(y + 1), iconColor);
  displayDrawRoundedRectangle(x, y, 9, 9, DISPLAY_WHITE, FALSE);
}

/// Dibuja pantalla de selección de slot.
static void uiDrawSlotSelect(uiMode_t mode, uint8_t selectedSlot,
                             uiSlotAction_t selectedAction,
                             const bool_t slotHasFile[UI_SLOT_COUNT]) {
  char text[] = "__1.sig";

  displayPlace(sprite_background, 0, 0, DISPLAY_WHITE);

  if (mode == UI_MODE_IR) {
    displayPlace(sprite_ir, 12, 13, DISPLAY_WHITE);
    text[0] = 'I';
    text[1] = 'R';
  } else {
    displayPlace(sprite_rf, 12, 13, DISPLAY_WHITE);
    text[0] = 'R';
    text[1] = 'F';
  }

  for (uint8_t i = 0; i < UI_SLOT_COUNT; i++) {
    uint8_t y = (uint8_t)(3 + 12U * i);
    bool_t selectedRow = (i == selectedSlot) ? TRUE : FALSE;
    DisplayColor textColor = selectedRow ? DISPLAY_BLACK : DISPLAY_WHITE;
    bool_t fileExists = slotHasFile[i];

    displayDrawRoundedRectangle(48, y, 34, 9, DISPLAY_WHITE, selectedRow);
    text[2] = (char)('1' + i);
    displayText(&aseprite_mini, text, 52, (uint8_t)(y + 2), textColor);

    if (fileExists) {
      uiDrawActionButton(89, y, &sprite_play, TRUE,
                         (selectedRow && selectedAction == UI_SLOT_ACTION_PLAY)
                             ? TRUE
                             : FALSE);
      uiDrawActionButton(
          99, y, &sprite_delete, TRUE,
          (selectedRow && selectedAction == UI_SLOT_ACTION_DELETE) ? TRUE
                                                                   : FALSE);
    } else {
      uiDrawActionButton(
          94, y, &sprite_record, TRUE,
          (selectedRow && selectedAction == UI_SLOT_ACTION_RECORD) ? TRUE
                                                                   : FALSE);
    }
  }

  displayUpdate();
}

/// Dibuja pantalla de captura en curso.
static void uiDrawCapturing(uiMode_t mode) {
  displayPlace(sprite_background, 0, 0, DISPLAY_WHITE);
  if (mode == UI_MODE_IR) {
    displayPlace(sprite_ir, 12, 13, DISPLAY_WHITE);
  } else {
    displayPlace(sprite_rf, 12, 13, DISPLAY_WHITE);
  }
  displayText(&aseprite_font, "Capturando...", 48, 25, DISPLAY_WHITE);
  displayUpdate();
}

/// Dibuja pantalla de captura exitosa.
static void uiDrawCaptureDone(uiMode_t mode) {
  displayPlace(sprite_background, 0, 0, DISPLAY_WHITE);
  if (mode == UI_MODE_IR) {
    displayPlace(sprite_ir, 12, 13, DISPLAY_WHITE);
  } else {
    displayPlace(sprite_rf, 12, 13, DISPLAY_WHITE);
  }
  displayText(&aseprite_font, "Señal capturada!", 48, 25, DISPLAY_WHITE);
  displayUpdate();
}

/// Dibuja pantalla de emisión en curso.
static void uiDrawReplaying(uiMode_t mode) {
  displayPlace(sprite_background, 0, 0, DISPLAY_WHITE);
  if (mode == UI_MODE_IR) {
    displayPlace(sprite_ir, 12, 13, DISPLAY_WHITE);
  } else {
    displayPlace(sprite_rf, 12, 13, DISPLAY_WHITE);
  }
  displayText(&aseprite_font, "Emitiendo señal...", 48, 25, DISPLAY_WHITE);
  displayUpdate();
}

/// Dibuja pantalla cuando no hay microSD.
static void uiDrawNeedSd(void) {
  displayPlace(sprite_background, 0, 0, DISPLAY_WHITE);
  displayText(&aseprite_font, "Insertá la microSD", 14, 28, DISPLAY_WHITE);
  displayUpdate();
}

/// Callback para cancelar captura desde UI.
static bool_t uiCaptureCancelCallback(void *context) {
  (void)context;
  if (!storageIsReady()) {
    return TRUE;
  }
  return swBackRead();
}

/// Arma path de slot según modo e índice.
static bool_t uiBuildSlotPath(uiMode_t mode, uint8_t slotIndex, char *path,
                              uint32_t pathSize) {
  if (path == NULL || pathSize < UI_SLOT_PATH_MAX ||
      slotIndex >= UI_SLOT_COUNT) {
    return FALSE;
  }

  path[0] = (mode == UI_MODE_IR) ? 'I' : 'R';
  path[1] = (mode == UI_MODE_IR) ? 'R' : 'F';
  path[2] = (char)('1' + slotIndex);
  path[3] = '.';
  path[4] = 's';
  path[5] = 'i';
  path[6] = 'g';
  path[7] = '\0';
  return TRUE;
}

/// Informa si una acción está habilitada para un slot.
static bool_t uiIsActionEnabled(uiSlotAction_t action, bool_t hasFile) {
  if (hasFile) {
    return (action == UI_SLOT_ACTION_PLAY || action == UI_SLOT_ACTION_DELETE)
               ? TRUE
               : FALSE;
  }
  return (action == UI_SLOT_ACTION_RECORD) ? TRUE : FALSE;
}

/// Cantidad de acciones visibles para el slot actual.
static uint8_t uiActionCountForSlot(bool_t hasFile) {
  return hasFile ? 2U : 1U;
}

/// Devuelve acción visible según índice para un slot.
static uiSlotAction_t uiActionAtIndex(bool_t hasFile, uint8_t index) {
  if (!hasFile) {
    return UI_SLOT_ACTION_RECORD;
  }
  return (index == 0U) ? UI_SLOT_ACTION_PLAY : UI_SLOT_ACTION_DELETE;
}

/// Devuelve índice visible de la acción para un slot.
static uint8_t uiActionIndex(bool_t hasFile, uiSlotAction_t action) {
  if (!hasFile) {
    return 0U;
  }
  return (action == UI_SLOT_ACTION_DELETE) ? 1U : 0U;
}

/// Asegura que la acción seleccionada sea válida para el slot.
static uiSlotAction_t uiNormalizeActionForSlot(uiSlotAction_t action,
                                               bool_t hasFile) {
  if (uiIsActionEnabled(action, hasFile)) {
    return action;
  }
  return hasFile ? UI_SLOT_ACTION_PLAY : UI_SLOT_ACTION_RECORD;
}

/// Refresca la existencia de archivo en cada slot.
static void uiRefreshSlots(uiMode_t mode, bool_t slotHasFile[UI_SLOT_COUNT]) {
  for (uint8_t i = 0; i < UI_SLOT_COUNT; i++) {
    char path[UI_SLOT_PATH_MAX];
    slotHasFile[i] = FALSE;

    if (!uiBuildSlotPath(mode, i, path, sizeof(path))) {
      continue;
    }
    slotHasFile[i] = storageFileExists(path);
  }
}

/// Guarda la última captura IR en archivo .sig.
static bool_t uiSaveLastIrCaptureToSig(const char *path) {
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

  if (storageSigSave(path, &record)) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] IR guardada en %s (%u edges)\r\n", path, pulseCount);
    if (hasNec) {
      printf("[tasks  ] [ui] Metadata NEC addr=0x%02X cmd=0x%02X\r\n", address,
             command);
    }
#endif
    return TRUE;
  }

#ifdef MEDIUM_DEBUG
  printf("[tasks  ] [ui] ERROR: no se pudo guardar %s\r\n", path);
#endif
  return FALSE;
}

/// Guarda la última captura RF en archivo .sig.
static bool_t uiSaveLastRfCaptureToSig(const char *path) {
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

  if (storageSigSave(path, &record)) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] RF guardada en %s (%u edges)\r\n", path, pulseCount);
#endif
    return TRUE;
  }

#ifdef MEDIUM_DEBUG
  printf("[tasks  ] [ui] ERROR: no se pudo guardar %s\r\n", path);
#endif
  return FALSE;
}

/// Reproduce una señal desde archivo .sig.
static bool_t uiPlaySigFromSlot(uiMode_t mode, const char *path) {
  sigRecordBuffer_t record;

  memset(&record, 0, sizeof(record));
  record.edges = uiSigEdges;
  record.edgesCapacity = UI_SIG_EDGE_BUFFER_MAX;
  record.metadata = uiSigMetadata;
  record.metadataCapacity = UI_SIG_META_BUFFER_MAX;

  if (!storageSigLoad(path, &record)) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] ERROR: no se pudo cargar %s para replay\r\n", path);
#endif
    return FALSE;
  }

  if (mode == UI_MODE_IR && record.signalType == SIG_SIGNAL_TYPE_IR) {
    return irReplayEdges(record.edges, record.edgeCount,
                         (record.flags & SIG_FLAG_START_LEVEL) ? 1U : 0U,
                         record.tickScale);
  }

  if (mode == UI_MODE_RF && record.signalType == SIG_SIGNAL_TYPE_RF) {
    return rfReplayEdges(record.edges, record.edgeCount,
                         (record.flags & SIG_FLAG_START_LEVEL) ? 1U : 0U,
                         record.tickScale);
  }

#ifdef MEDIUM_DEBUG
  printf("[tasks  ] [ui] ERROR: tipo .sig no coincide con modo (%s)\r\n", path);
#endif
  return FALSE;
}

/// Ejecuta captura y guardado en slot.
static bool_t uiCaptureToSlot(uiMode_t mode, const char *path) {
  bool_t captureOk = FALSE;
  bool_t saveOk = FALSE;

  uiDrawCapturing(mode);

  if (mode == UI_MODE_IR) {
    captureOk = irRecordNoStartTimeoutWithCancel(uiCaptureCancelCallback, NULL);
    if (captureOk && storageIsReady()) {
      saveOk = uiSaveLastIrCaptureToSig(path);
    }
  } else {
    captureOk = rfCapture433MHzWithCancel(uiCaptureCancelCallback, NULL);
    if (captureOk && storageIsReady()) {
      saveOk = uiSaveLastRfCaptureToSig(path);
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

  if (captureOk && saveOk) {
    uiDrawCaptureDone(mode);
    vTaskDelay(pdMS_TO_TICKS(2000));
    return TRUE;
  }

  return FALSE;
}

/// Ejecuta una acción sobre un slot.
static bool_t uiRunSlotAction(uiMode_t mode, uint8_t slotIndex,
                              uiSlotAction_t action,
                              bool_t slotHasFile[UI_SLOT_COUNT]) {
  char path[UI_SLOT_PATH_MAX];
  bool_t actionOk = FALSE;
  TickType_t replayStartTicks = 0;

  if (!uiBuildSlotPath(mode, slotIndex, path, sizeof(path))) {
    return FALSE;
  }

  if (!uiIsActionEnabled(action, slotHasFile[slotIndex])) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] Accion no disponible en slot %u\r\n",
           (unsigned)(slotIndex + 1U));
#endif
    return FALSE;
  }

  if (action == UI_SLOT_ACTION_RECORD) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] Record slot %u\r\n", (unsigned)(slotIndex + 1U));
#endif
    actionOk = uiCaptureToSlot(mode, path);
  } else if (action == UI_SLOT_ACTION_PLAY) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] Play slot %u\r\n", (unsigned)(slotIndex + 1U));
#endif
    uiDrawReplaying(mode);
    replayStartTicks = xTaskGetTickCount();
    actionOk = uiPlaySigFromSlot(mode, path);
    {
      TickType_t replayElapsed = xTaskGetTickCount() - replayStartTicks;
      TickType_t minTicks = pdMS_TO_TICKS(2000);
      if (replayElapsed < minTicks) {
        vTaskDelay(minTicks - replayElapsed);
      }
    }
  } else if (action == UI_SLOT_ACTION_DELETE) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] Delete slot %u\r\n", (unsigned)(slotIndex + 1U));
#endif
    actionOk = storageFileDelete(path);
  }

  if (storageIsReady()) {
    uiRefreshSlots(mode, slotHasFile);
  }
  return actionOk;
}

/// Task de interacción de usuario.
static void uiTask(void *taskParam) {
  uiMode_t mode = UI_MODE_IR;
  uiScreen_t screen = UI_SCREEN_MAIN;
  uint8_t selectedSlot = 0U;
  uiSlotAction_t selectedAction = UI_SLOT_ACTION_RECORD;
  bool_t slotHasFile[UI_SLOT_COUNT] = {FALSE};
  int8_t lastJoystickX = 0;
  int8_t lastJoystickY = 0;
  bool_t lastEnter = FALSE;
  bool_t lastBack = FALSE;
  bool_t showingNeedSd = FALSE;

  (void)taskParam;
  uiDrawMain(mode);

  while (1) {
    bool_t enterNow;
    bool_t backNow;
    JoystickState joystick;

    storageUpdate();

    if (!storageIsReady()) {
      if (!showingNeedSd) {
        showingNeedSd = TRUE;
        uiDrawNeedSd();
#ifdef MEDIUM_DEBUG
        printf("[tasks  ] [ui] SD no disponible\r\n");
#endif
      }
      lastJoystickX = joystickRead().x;
      lastJoystickY = joystickRead().y;
      lastEnter = swEnterRead();
      lastBack = swBackRead();
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (showingNeedSd) {
      showingNeedSd = FALSE;
      if (screen == UI_SCREEN_SLOT_SELECT) {
        uiRefreshSlots(mode, slotHasFile);
        selectedAction =
            uiNormalizeActionForSlot(selectedAction, slotHasFile[selectedSlot]);
        uiDrawSlotSelect(mode, selectedSlot, selectedAction, slotHasFile);
      } else {
        uiDrawMain(mode);
      }
    }

    joystick = joystickRead();
    enterNow = swEnterRead();
    backNow = swBackRead();

    if (screen == UI_SCREEN_MAIN) {
      if (joystick.x != lastJoystickX) {
        if (joystick.x == -1) {
          mode = UI_MODE_IR;
          uiDrawMain(mode);
        } else if (joystick.x == 1) {
          mode = UI_MODE_RF;
          uiDrawMain(mode);
        }
      }

      if (enterNow && !lastEnter) {
        screen = UI_SCREEN_SLOT_SELECT;
        selectedSlot = 0U;
        uiRefreshSlots(mode, slotHasFile);
        selectedAction =
            uiNormalizeActionForSlot(UI_SLOT_ACTION_RECORD, slotHasFile[0]);
        uiDrawSlotSelect(mode, selectedSlot, selectedAction, slotHasFile);
      }
    } else {
      if (joystick.y != lastJoystickY) {
        if (joystick.y == -1 && selectedSlot > 0U) {
          selectedSlot--;
          selectedAction = uiNormalizeActionForSlot(selectedAction,
                                                    slotHasFile[selectedSlot]);
          uiDrawSlotSelect(mode, selectedSlot, selectedAction, slotHasFile);
        } else if (joystick.y == 1 && selectedSlot < (UI_SLOT_COUNT - 1U)) {
          selectedSlot++;
          selectedAction = uiNormalizeActionForSlot(selectedAction,
                                                    slotHasFile[selectedSlot]);
          uiDrawSlotSelect(mode, selectedSlot, selectedAction, slotHasFile);
        }
      }

      if (joystick.x != lastJoystickX) {
        bool_t hasFile = slotHasFile[selectedSlot];
        uint8_t actionIndex = uiActionIndex(hasFile, selectedAction);
        uint8_t actionCount = uiActionCountForSlot(hasFile);

        if (joystick.x == -1) {
          if (actionIndex > 0U) {
            actionIndex--;
            selectedAction = uiActionAtIndex(hasFile, actionIndex);
            uiDrawSlotSelect(mode, selectedSlot, selectedAction, slotHasFile);
          }
        } else if (joystick.x == 1) {
          if ((actionIndex + 1U) < actionCount) {
            actionIndex++;
            selectedAction = uiActionAtIndex(hasFile, actionIndex);
            uiDrawSlotSelect(mode, selectedSlot, selectedAction, slotHasFile);
          }
        }
      }

      if (enterNow && !lastEnter) {
        uiSlotAction_t actionBefore = selectedAction;
        bool_t actionOk =
            uiRunSlotAction(mode, selectedSlot, selectedAction, slotHasFile);

        if (actionBefore == UI_SLOT_ACTION_RECORD && actionOk &&
            slotHasFile[selectedSlot]) {
          selectedAction = UI_SLOT_ACTION_PLAY;
        } else {
          selectedAction = uiNormalizeActionForSlot(selectedAction,
                                                    slotHasFile[selectedSlot]);
        }
        if (storageIsReady()) {
          uiDrawSlotSelect(mode, selectedSlot, selectedAction, slotHasFile);
        }
      }

      if (backNow && !lastBack) {
        screen = UI_SCREEN_MAIN;
        uiDrawMain(mode);
      }
    }

    lastJoystickX = joystick.x;
    lastJoystickY = joystick.y;
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
