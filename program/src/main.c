//===----------------------------------------------------------------------===//
///
/// \file
/// Inicio del programa. Contiene llamados a inicialización y bucle principal.
///
//===----------------------------------------------------------------------===//

#include "main.h"
#include "drivers/buttons.h"
#include "modules/display.h"
#include "modules/ir.h"
#include "modules/rf.h"
#include "modules/sprites.h"
#include "modules/storage.h"
#include "utils/sig.h"

/// Guarda la última captura IR en archivo .sig.
static void saveLastIrCaptureToSig(void) {
  const IRPulse *pulses = NULL;
  uint16_t pulseCount = 0;
  uint32_t edges[IR_MAX_PULSES];
  sigRecord_t record;
  uint8_t metadata[16];
  uint32_t metadataSize = 0;
  uint8_t address = 0;
  uint8_t command = 0;
  bool_t hasNec = FALSE;

  if (!irGetLastCapture(&pulses, &pulseCount) || pulses == NULL || pulseCount == 0) {
#ifdef MEDIUM_DEBUG
    printf("[main] WARN: no hay captura IR valida para guardar\r\n");
#endif
    return;
  }

  for (uint16_t i = 0; i < pulseCount; i++) {
    edges[i] = pulses[i].durationUs;
  }

  hasNec = irDecodeLastNec(&address, &command);
  if (hasNec) {
    if (!sigMetadataAppendTlv(metadata, sizeof(metadata), &metadataSize,
                              SIG_META_NEC_ADDR, &address,
                              sizeof(address)) ||
        !sigMetadataAppendTlv(metadata, sizeof(metadata), &metadataSize,
                              SIG_META_NEC_CMD, &command,
                              sizeof(command))) {
#ifdef MEDIUM_DEBUG
      printf("[main] WARN: no se pudo serializar metadata NEC\r\n");
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
    printf("[main] IR guardada en IR001.sig (%u edges)\r\n", pulseCount);
    if (hasNec) {
      printf("[main] Metadata NEC addr=0x%02X cmd=0x%02X\r\n", address, command);
    }
#endif
  } else {
#ifdef MEDIUM_DEBUG
    printf("[main] ERROR: no se pudo guardar IR001.sig\r\n");
#endif
  }
}

int main(void) {
  // Inicializar placa, puertos y protocolos
  boardConfig();
  adcConfig(ADC_ENABLE);   // Usado por buttons.c
  i2cConfig(I2C0, 100000); // Usado por sh1106.c
  spiConfig(SPI0);         // Usado por cc1101.c y storage.c

#ifdef MEDIUM_DEBUG
  // Configurar UART para debug
  uartConfig(UART_USB, 115200);
  printf("\r\n\r\n========== Terminal de Médium\r\n");
#endif

  // Inicializar componentes
  buttonsInit();
  displayInit();
  storageInit();
  irInit();
  rfInit();

#ifdef MEDIUM_DEBUG
  printf("========== Componentes inicializados\r\n");
#endif

  bool_t changed = true;
  int8_t selected = -1;
  int8_t last_joystick = 0;

  while (1) {
    JoystickState joystick = joystickRead();
    if (joystick.x != last_joystick) {
      if (joystick.x != 0) {
        changed = true;
        selected = joystick.x;
        if (joystick.x == 1) {
          // rfCapture433MHz();
          if (irRecord()) {
            saveLastIrCaptureToSig();
          }
        } else {
          // rfReplayCaptured();
          irReplay();
        }
      }
      last_joystick = joystick.x;
    }

    if (changed) {
      changed = false;
      displayPlace(sprite_background, 0, 0);
      displayPlace(sprite_title, 36, 4);
      displayPlace(sprite_ir, 26, 22);
      displayPlace(sprite_rf, 71, 22);
      if (selected == -1) {
        displayPlace(sprite_lselector, 19, 32);
        displayPlace(sprite_rselector, 59, 32);
      } else if (selected == 1) {
        displayPlace(sprite_lselector, 64, 32);
        displayPlace(sprite_rselector, 104, 32);
      }

      displayUpdate();
    }
    // if (irRecord()) {
    //   uint8_t address, command;
    //   if (irDecodeLastNec(&address, &command)) {
    //     printf("NEC %d - %d\r\n", address, command);
    //   } else {
    //     printf("No NEC\r\n");
    //   }
    // }
    delay(100);
  }

  return 0;
}
