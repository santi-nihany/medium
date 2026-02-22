//===----------------------------------------------------------------------===//
///
/// \file
/// Captura y reproducción de tramas RF con CC1101.
///
//===----------------------------------------------------------------------===//

#ifndef RF_H
#define RF_H

#include "drivers/cc1101.h"
#include "main.h"

/// Cantidad máxima de duraciones (en us) capturadas.
#define RF_CAPTURE_PULSES_MAX 512
/// Cantidad de bits esperados para protocolo Princeton.
#define RF_PRINCETON_BITS 24

typedef bool_t (*rfCancelCallback_t)(void *context);

typedef struct {
  uint32_t key;
  uint16_t teUs;
  uint8_t guardTime;
  uint8_t bitCount;
} rfPrincetonInfo_t;

bool_t rfInit(void);
bool_t rfSetCaptureConfig(const cc1101OokConfig_t *config);
bool_t rfCapture(void);
bool_t rfCaptureWithCancel(rfCancelCallback_t cancelCallback, void *context);
bool_t rfRunFrequencyAnalyzer(void);
bool_t rfReplayCaptured(void);
bool_t rfReplayEdges(const uint32_t *edges, uint32_t edgeCount,
                     uint8_t startLevel, int8_t tickScale);
bool_t rfHasCapture(void);
bool_t rfGetLastCapture(const uint16_t **pulsesUs, uint16_t *count,
                        bool_t *firstLevel);
bool_t rfGetActiveConfig(cc1101OokConfig_t *config);
bool_t rfGetLastCaptureConfig(cc1101OokConfig_t *config);
bool_t rfDecodePrinceton(const uint16_t *pulsesUs, uint16_t count,
                         bool_t firstLevel, rfPrincetonInfo_t *infoOut);
bool_t rfDecodeLastPrinceton(rfPrincetonInfo_t *infoOut);

#endif
