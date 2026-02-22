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
#define UI_TASK_STACK_WORDS 1024
/// Prioridad de la task UI.
#define UI_TASK_PRIORITY (tskIDLE_PRIORITY + 2)
/// Cantidad de slots por modo.
#define UI_SLOT_COUNT 5U
/// Tamaño máximo de path para nombre de slot.
#define UI_SLOT_PATH_MAX 16U
/// Capacidad máxima de edges al cargar un .sig para replay.
#define UI_SIG_EDGE_BUFFER_MAX RF_CAPTURE_PULSES_MAX
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

typedef enum {
  UI_RF_FREQ_433 = 0,
  UI_RF_FREQ_315 = 1,
} uiRfFrequencyOption_t;

typedef enum {
  UI_RF_MOD_AM650 = 0,
  UI_RF_MOD_AM270 = 1,
} uiRfModOption_t;

/// Buffers estáticos para load/replay de .sig.
static uint32_t uiSigEdges[UI_SIG_EDGE_BUFFER_MAX];
static uint8_t uiSigMetadata[UI_SIG_META_BUFFER_MAX];
static uiRfFrequencyOption_t uiRfSelectedFreq = UI_RF_FREQ_433;
static uiRfModOption_t uiRfSelectedMod = UI_RF_MOD_AM650;

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

/// Escribe u32 little-endian.
static void uiWriteLe32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFU);
  dst[1] = (uint8_t)((value >> 8) & 0xFFU);
  dst[2] = (uint8_t)((value >> 16) & 0xFFU);
  dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

/// Lee u32 little-endian.
static uint32_t uiReadLe32(const uint8_t *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
         ((uint32_t)src[3] << 24);
}

/// Convierte selección de UI a configuración RF.
static cc1101OokConfig_t uiRfConfigFromSelection(uiRfFrequencyOption_t freq,
                                                 uiRfModOption_t mod) {
  cc1101OokConfig_t config;

  memset(&config, 0, sizeof(config));
  config.band =
      (freq == UI_RF_FREQ_315) ? CC1101_BAND_315MHZ : CC1101_BAND_433MHZ;
  config.frequencyHz = (freq == UI_RF_FREQ_315) ? 315000000UL : 433920000UL;
  if (mod == UI_RF_MOD_AM270) {
    config.preset = CC1101_OOK_PRESET_AM270_ASYNC;
  } else {
    config.preset = CC1101_OOK_PRESET_AM650_ASYNC;
  }
  config.paTable = CC1101_OOK_PA_TABLE_433;
  config.paTableSize = 8U;
  return config;
}

/// Convierte frecuencia a opción de UI.
static uiRfFrequencyOption_t uiRfFrequencyToOption(uint32_t frequencyHz) {
  return (frequencyHz >= 360000000UL) ? UI_RF_FREQ_433 : UI_RF_FREQ_315;
}

/// Convierte preset a opción de UI.
static uiRfModOption_t uiRfPresetToOption(cc1101ModPreset_t preset) {
  if (preset == CC1101_OOK_PRESET_AM270_ASYNC) {
    return UI_RF_MOD_AM270;
  }
  return UI_RF_MOD_AM650;
}

/// Convierte preset de CC1101 a enum de metadata .sig.
static sigRfModulation_t uiRfPresetToSigModulation(cc1101ModPreset_t preset) {
  if (preset == CC1101_OOK_PRESET_AM270_ASYNC) {
    return SIG_RF_MOD_AM270;
  }
  return SIG_RF_MOD_AM650;
}

/// Convierte enum de metadata .sig a preset de CC1101.
static cc1101ModPreset_t uiSigModulationToRfPreset(sigRfModulation_t modulation) {
  if (modulation == SIG_RF_MOD_AM270) {
    return CC1101_OOK_PRESET_AM270_ASYNC;
  }
  return CC1101_OOK_PRESET_AM650_ASYNC;
}

/// Dibuja selector de configuración RF para grabación.
static void uiDrawRfRecordConfig(uiRfFrequencyOption_t frequency,
                                 uiRfModOption_t modulation,
                                 uint8_t selectedRow) {
  char line1[] = ">Freq: 433.920";
  char line2[] = " Mod : AM650";

  displayPlace(sprite_background, 0, 0, DISPLAY_WHITE);
  displayPlace(sprite_rf, 12, 13, DISPLAY_WHITE);
  displayText(&aseprite_font, "Configuración", 48, 15, DISPLAY_WHITE);

  line1[0] = (selectedRow == 0U) ? '>' : ' ';
  if (frequency == UI_RF_FREQ_315) {
    line1[7] = '3';
    line1[8] = '1';
    line1[9] = '5';
    line1[10] = '.';
    line1[11] = '0';
    line1[12] = '0';
    line1[13] = '0';
  }

  line2[0] = (selectedRow == 1U) ? '>' : ' ';
  if (modulation == UI_RF_MOD_AM270) {
    line2[7] = 'A';
    line2[8] = 'M';
    line2[9] = '2';
    line2[10] = '7';
    line2[11] = '0';
  } else {
    line2[7] = 'A';
    line2[8] = 'M';
    line2[9] = '6';
    line2[10] = '5';
    line2[11] = '0';
  }

  displayText(&aseprite_font, line1, 48, 34, DISPLAY_WHITE);
  displayText(&aseprite_font, line2, 48, 44, DISPLAY_WHITE);
  displayUpdate();
}

/// Ejecuta selector RF previo a grabación.
static bool_t uiSelectRfRecordConfig(cc1101OokConfig_t *configOut) {
  uint8_t selectedRow = 0U;
  int8_t lastX = joystickRead().x;
  int8_t lastY = joystickRead().y;
  bool_t lastEnter = swEnterRead();
  bool_t lastBack = swBackRead();

  if (configOut == NULL) {
    return FALSE;
  }

  uiDrawRfRecordConfig(uiRfSelectedFreq, uiRfSelectedMod, selectedRow);

  while (storageIsReady()) {
    JoystickState joystick = joystickRead();
    bool_t enterNow = swEnterRead();
    bool_t backNow = swBackRead();

    if (joystick.y != 0 && joystick.y != lastY) {
      if (joystick.y == 1) {
        selectedRow = 1U;
        uiDrawRfRecordConfig(uiRfSelectedFreq, uiRfSelectedMod, selectedRow);
      } else if (joystick.y == -1) {
        selectedRow = 0U;
        uiDrawRfRecordConfig(uiRfSelectedFreq, uiRfSelectedMod, selectedRow);
      }
    }

    if (joystick.x != 0 && joystick.x != lastX) {
      if (selectedRow == 0U) {
        uiRfSelectedFreq = (uiRfSelectedFreq == UI_RF_FREQ_433)
                               ? UI_RF_FREQ_315
                               : UI_RF_FREQ_433;
      } else {
        if (joystick.x > 0) {
          uiRfSelectedMod =
              (uiRfModOption_t)((((uint8_t)uiRfSelectedMod) + 1U) % 2U);
        } else {
          uiRfSelectedMod = (uiRfModOption_t)(
              (((uint8_t)uiRfSelectedMod) + 2U - 1U) % 2U);
        }
      }
      uiDrawRfRecordConfig(uiRfSelectedFreq, uiRfSelectedMod, selectedRow);
    }

    if (enterNow && !lastEnter) {
      *configOut = uiRfConfigFromSelection(uiRfSelectedFreq, uiRfSelectedMod);
      return TRUE;
    }
    if (backNow && !lastBack) {
      return FALSE;
    }

    lastX = joystick.x;
    lastY = joystick.y;
    lastEnter = enterNow;
    lastBack = backNow;
    vTaskDelay(pdMS_TO_TICKS(40));
  }

  return FALSE;
}

/// Carga config RF desde metadata TLV si existe.
static bool_t uiRfConfigFromMetadata(const uint8_t *metadata,
                                     uint32_t metadataSize,
                                     cc1101OokConfig_t *configOut) {
  uint32_t pos = 0U;
  bool_t hasFreq = FALSE;
  bool_t hasMod = FALSE;
  uint32_t frequencyHz = 433920000UL;
  sigRfModulation_t modulation = SIG_RF_MOD_AM650;

  if (metadata == NULL || configOut == NULL || metadataSize == 0U) {
    return FALSE;
  }

  while ((pos + 2U) <= metadataSize) {
    uint8_t type = metadata[pos++];
    uint8_t len = metadata[pos++];
    if ((pos + len) > metadataSize) {
      return FALSE;
    }

    if (type == SIG_META_RF_FREQ_HZ && len == 4U) {
      frequencyHz = uiReadLe32(&metadata[pos]);
      hasFreq = TRUE;
    } else if (type == SIG_META_RF_MODULATION && len == 1U) {
      if (metadata[pos] == (uint8_t)SIG_RF_MOD_AM270) {
        modulation = SIG_RF_MOD_AM270;
        hasMod = TRUE;
      } else if (metadata[pos] == (uint8_t)SIG_RF_MOD_AM650) {
        modulation = SIG_RF_MOD_AM650;
        hasMod = TRUE;
      }
    }

    pos += len;
  }

  if (!hasFreq && !hasMod) {
    return FALSE;
  }

  *configOut = uiRfConfigFromSelection(uiRfFrequencyToOption(frequencyHz),
                                       uiRfPresetToOption(
                                           uiSigModulationToRfPreset(modulation)));
  return TRUE;
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
  uint16_t startIndex = 0U;
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

  while (startIndex < pulseCount && pulses[startIndex].level != 0U) {
    startIndex++;
  }
  if (startIndex >= pulseCount) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] WARN: captura IR sin MARK inicial util\r\n");
#endif
    return FALSE;
  }

  for (uint16_t i = startIndex; i < pulseCount; i++) {
    uiSigEdges[i - startIndex] = pulses[i].durationUs;
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
  record.flags = 0U;     // normalizado a MARK inicial (nivel 0)
  record.tickScale = -6; // 1us por tick
  record.edgeCount = (uint32_t)(pulseCount - startIndex);
  record.edges = uiSigEdges;
  record.metadata = hasNec ? metadata : NULL;
  record.metadataSize = metadataSize;
  if (hasNec) {
    record.flags |= SIG_FLAG_HAS_METADATA;
  }

  if (storageSigSave(path, &record)) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] IR guardada en %s (%lu edges)\r\n", path,
           (unsigned long)record.edgeCount);
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
  sigRecord_t record;
  cc1101OokConfig_t captureConfig;
  uint8_t metadata[64];
  uint8_t freqBytes[4];
  uint8_t keyBytes[4];
  uint8_t teBytes[2];
  uint32_t metadataSize = 0U;
  uint8_t modulationByte;
  rfPrincetonInfo_t princetonInfo;
  bool_t hasPrinceton = FALSE;

  if (!rfGetLastCapture(&pulses, &pulseCount, &firstLevel) || pulses == NULL ||
      pulseCount == 0) {
#ifdef MEDIUM_DEBUG
    printf("[tasks  ] [ui] WARN: no hay captura RF valida para guardar\r\n");
#endif
    return FALSE;
  }

  if (!rfGetLastCaptureConfig(&captureConfig)) {
    if (!rfGetActiveConfig(&captureConfig)) {
      captureConfig =
          uiRfConfigFromSelection(uiRfSelectedFreq, uiRfSelectedMod);
    }
  }

  uiWriteLe32(freqBytes, captureConfig.frequencyHz);
  if (!sigMetadataAppendTlv(metadata, sizeof(metadata), &metadataSize,
                            SIG_META_RF_FREQ_HZ, freqBytes, 4U)) {
    return FALSE;
  }
  modulationByte = (uint8_t)uiRfPresetToSigModulation(captureConfig.preset);
  if (!sigMetadataAppendTlv(metadata, sizeof(metadata), &metadataSize,
                            SIG_META_RF_MODULATION, &modulationByte, 1U)) {
    return FALSE;
  }

  hasPrinceton = rfDecodeLastPrinceton(&princetonInfo);
  if (hasPrinceton) {
    uiWriteLe32(keyBytes, princetonInfo.key);
    teBytes[0] = (uint8_t)(princetonInfo.teUs & 0xFFU);
    teBytes[1] = (uint8_t)((princetonInfo.teUs >> 8) & 0xFFU);

    if (!sigMetadataAppendTlv(metadata, sizeof(metadata), &metadataSize,
                              SIG_META_RF_PRINCETON_KEY, keyBytes, 4U) ||
        !sigMetadataAppendTlv(metadata, sizeof(metadata), &metadataSize,
                              SIG_META_RF_PRINCETON_TE_US, teBytes, 2U) ||
        !sigMetadataAppendTlv(metadata, sizeof(metadata), &metadataSize,
                              SIG_META_RF_PRINCETON_GUARD,
                              &princetonInfo.guardTime, 1U) ||
        !sigMetadataAppendTlv(metadata, sizeof(metadata), &metadataSize,
                              SIG_META_RF_PRINCETON_BITS,
                              &princetonInfo.bitCount, 1U)) {
      hasPrinceton = FALSE;
    }
  }

  for (uint16_t i = 0; i < pulseCount; i++) {
    uiSigEdges[i] = pulses[i];
  }

  memset(&record, 0, sizeof(record));
  record.signalType = SIG_SIGNAL_TYPE_RF;
  record.flags = firstLevel ? SIG_FLAG_START_LEVEL : 0U;
  record.flags |= SIG_FLAG_HAS_METADATA | SIG_FLAG_HAS_PROFILE;
  record.tickScale = -6; // 1us por tick
  record.edgeCount = pulseCount;
  record.edges = uiSigEdges;
  record.metadata = metadata;
  record.metadataSize = metadataSize;

  if (storageSigSave(path, &record)) {
#ifdef MEDIUM_DEBUG
    {
      const char *modName = "AM650";
      if (captureConfig.preset == CC1101_OOK_PRESET_AM270_ASYNC) {
        modName = "AM270";
      }
      printf("[tasks  ] [ui] RF guardada en %s (%u edges, %luHz, %s)\r\n", path,
             pulseCount, (unsigned long)captureConfig.frequencyHz, modName);
      if (hasPrinceton) {
        printf("[tasks  ] [ui] Metadata Princeton key=0x%06lX te=%uus gt=%u bits=%u\r\n",
               (unsigned long)princetonInfo.key, (unsigned)princetonInfo.teUs,
               (unsigned)princetonInfo.guardTime, (unsigned)princetonInfo.bitCount);
      }
    }
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
  cc1101OokConfig_t rfConfig;

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
    if ((record.flags & SIG_FLAG_HAS_METADATA) != 0U &&
        uiRfConfigFromMetadata(record.metadata, record.metadataSize,
                               &rfConfig)) {
      (void)rfSetCaptureConfig(&rfConfig);
      uiRfSelectedFreq = uiRfFrequencyToOption(rfConfig.frequencyHz);
      uiRfSelectedMod = uiRfPresetToOption(rfConfig.preset);
    }
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
  cc1101OokConfig_t rfConfig;

  if (mode == UI_MODE_IR) {
    uiDrawCapturing(mode);
    captureOk = irRecordNoStartTimeoutWithCancel(uiCaptureCancelCallback, NULL);
    if (captureOk && storageIsReady()) {
      saveOk = uiSaveLastIrCaptureToSig(path);
    }
  } else {
    if (!uiSelectRfRecordConfig(&rfConfig)) {
#ifdef MEDIUM_DEBUG
      printf("[tasks  ] [ui] Seleccion RF cancelada\r\n");
#endif
      return FALSE;
    }
    if (!rfSetCaptureConfig(&rfConfig)) {
#ifdef MEDIUM_DEBUG
      printf("[tasks  ] [ui] ERROR: no se pudo aplicar config RF\r\n");
#endif
      return FALSE;
    }

    uiDrawCapturing(mode);
    captureOk = rfCaptureWithCancel(uiCaptureCancelCallback, NULL);
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
  {
    cc1101OokConfig_t config;
    if (rfGetActiveConfig(&config)) {
      uiRfSelectedFreq = uiRfFrequencyToOption(config.frequencyHz);
      uiRfSelectedMod = uiRfPresetToOption(config.preset);
    }
  }

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
