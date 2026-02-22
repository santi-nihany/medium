#ifndef SIG_H
#define SIG_H

#include "main.h"

/// Magic string para identificar archivos de señal.
#define SIG_MAGIC "SIG1"
/// Tamaño fijo del header serializado.
#define SIG_HEADER_SIZE_BYTES 32U

/// Bit de flags: nivel lógico inicial de la señal.
#define SIG_FLAG_START_LEVEL (1U << 0)
/// Bit de flags: existe metadata TLV/binaria.
#define SIG_FLAG_HAS_METADATA (1U << 1)
/// Bit de flags: existe perfil/protocolo específico.
#define SIG_FLAG_HAS_PROFILE (1U << 2)

/// Tipos de señal soportados por el formato .sig.
typedef enum {
  SIG_SIGNAL_TYPE_IR = 1,
  SIG_SIGNAL_TYPE_RF = 2,
} sigSignalType_t;

/// Tipos de metadata TLV para .sig.
typedef enum {
  SIG_META_NEC_ADDR = 1,
  SIG_META_NEC_CMD = 2,
  SIG_META_RF_FREQ_HZ = 16,
  SIG_META_RF_MODULATION = 17,
  SIG_META_RF_PRINCETON_KEY = 18,
  SIG_META_RF_PRINCETON_TE_US = 19,
  SIG_META_RF_PRINCETON_GUARD = 20,
  SIG_META_RF_PRINCETON_BITS = 21,
} sigMetadataType_t;

/// Enum de modulación RF serializable en metadata.
typedef enum {
  SIG_RF_MOD_AM270 = 1,
  SIG_RF_MOD_AM650 = 2,
} sigRfModulation_t;

/// Callback de escritura de stream para serializar .sig.
typedef bool_t (*sigWriteCallback_t)(void *context, const uint8_t *data,
                                     uint32_t size);
/// Callback de lectura de stream para parsear .sig.
typedef bool_t (*sigReadCallback_t)(void *context, uint8_t *data,
                                    uint32_t size);

/// Vista de datos para escribir un archivo .sig.
typedef struct {
  uint8_t signalType;
  uint8_t flags;
  int8_t tickScale;
  uint32_t edgeCount;
  const uint32_t *edges;
  const uint8_t *metadata;
  uint32_t metadataSize;
} sigRecord_t;

/// Vista de buffers para leer un archivo .sig.
typedef struct {
  uint8_t signalType;
  uint8_t flags;
  int8_t tickScale;
  uint32_t edgeCount;
  uint32_t *edges;
  uint32_t edgesCapacity;
  uint8_t *metadata;
  uint32_t metadataCapacity;
  uint32_t metadataSize;
} sigRecordBuffer_t;

bool_t sigWriteRecord(sigWriteCallback_t writeCallback, void *context,
                      const sigRecord_t *record);
bool_t sigReadRecord(sigReadCallback_t readCallback, void *context,
                     sigRecordBuffer_t *record);
bool_t sigMetadataAppendTlv(uint8_t *buffer, uint32_t bufferCapacity,
                            uint32_t *currentSize, uint8_t type,
                            const uint8_t *value, uint8_t valueLength);

#endif
