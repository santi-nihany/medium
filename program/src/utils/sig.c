//===----------------------------------------------------------------------===//
///
/// \file
/// Operaciones con archivos .SIG
///
//===----------------------------------------------------------------------===//

#include "utils/sig.h"

/// Valor de escape para codificar duraciones extendidas.
#define SIG_EDGE_EXTENDED_16 0xFFFFU

/// Escribe un entero de 16 bits en little-endian.
static void sigWriteLe16(uint8_t *dst, uint16_t value) {
  dst[0] = (uint8_t)(value & 0xFFU);
  dst[1] = (uint8_t)((value >> 8) & 0xFFU);
}

/// Escribe un entero de 32 bits en little-endian.
static void sigWriteLe32(uint8_t *dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xFFU);
  dst[1] = (uint8_t)((value >> 8) & 0xFFU);
  dst[2] = (uint8_t)((value >> 16) & 0xFFU);
  dst[3] = (uint8_t)((value >> 24) & 0xFFU);
}

/// Lee un entero de 16 bits en little-endian.
static uint16_t sigReadLe16(const uint8_t *src) {
  return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

/// Lee un entero de 32 bits en little-endian.
static uint32_t sigReadLe32(const uint8_t *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
         ((uint32_t)src[3] << 24);
}

/// Actualiza CRC32 (polinomio IEEE 0xEDB88320).
static uint32_t sigCrc32Update(uint32_t crc, const uint8_t *data,
                               uint32_t size) {
  for (uint32_t i = 0; i < size; i++) {
    crc ^= (uint32_t)data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      uint32_t lsb = crc & 1U;
      crc >>= 1;
      if (lsb != 0U) {
        crc ^= 0xEDB88320UL;
      }
    }
  }
  return crc;
}

/// Calcula CRC32 final para un bloque.
static uint32_t sigCrc32(const uint8_t *data, uint32_t size) {
  return sigCrc32Update(0xFFFFFFFFUL, data, size) ^ 0xFFFFFFFFUL;
}

/// Tamaño serializado de una duración en ticks.
static uint32_t sigEdgeEncodedSize(uint32_t ticks) {
  return (ticks <= 0xFFFEUL) ? 2U : 6U;
}

/// Escribe una duración codificada al stream.
static bool_t sigWriteEdge(sigWriteCallback_t writeCallback, void *context,
                           uint32_t ticks) {
  uint8_t buffer[6];

  if (ticks <= 0xFFFEUL) {
    sigWriteLe16(buffer, (uint16_t)ticks);
    return writeCallback(context, buffer, 2U);
  }

  sigWriteLe16(buffer, SIG_EDGE_EXTENDED_16);
  sigWriteLe32(&buffer[2], ticks);
  return writeCallback(context, buffer, 6U);
}

/// Lee una duración codificada desde stream.
static bool_t sigReadEdge(sigReadCallback_t readCallback, void *context,
                          uint32_t *ticks, uint8_t *raw, uint32_t *rawSize) {
  uint8_t head[2];

  if (ticks == NULL || raw == NULL || rawSize == NULL) {
    return FALSE;
  }

  if (!readCallback(context, head, sizeof(head))) {
    return FALSE;
  }
  raw[0] = head[0];
  raw[1] = head[1];

  {
    uint16_t shortTicks = sigReadLe16(head);
    if (shortTicks != SIG_EDGE_EXTENDED_16) {
      *ticks = shortTicks;
      *rawSize = 2U;
      return TRUE;
    }
  }

  if (!readCallback(context, &raw[2], 4U)) {
    return FALSE;
  }
  *ticks = sigReadLe32(&raw[2]);
  *rawSize = 6U;
  return TRUE;
}

/// Calcula el tamaño total del bloque de duraciones serializadas.
static bool_t sigComputeEdgesSize(const sigRecord_t *record,
                                  uint32_t *edgesSizeOut) {
  uint32_t total = 0U;

  if (record == NULL || edgesSizeOut == NULL) {
    return FALSE;
  }

  for (uint32_t i = 0; i < record->edgeCount; i++) {
    uint32_t size = sigEdgeEncodedSize(record->edges[i]);
    if ((0xFFFFFFFFUL - total) < size) {
      return FALSE;
    }
    total += size;
  }

  *edgesSizeOut = total;
  return TRUE;
}

/// Valida los parámetros de escritura de un .sig.
static bool_t sigValidateWriteRecord(const sigRecord_t *record) {
  if (record == NULL) {
    return FALSE;
  }
  if (record->signalType != SIG_SIGNAL_TYPE_IR &&
      record->signalType != SIG_SIGNAL_TYPE_RF) {
    return FALSE;
  }
  if (record->edgeCount > 0U && record->edges == NULL) {
    return FALSE;
  }
  if (record->metadataSize > 0U && record->metadata == NULL) {
    return FALSE;
  }
  return TRUE;
}

/// Escribe un registro .sig usando callbacks genéricos.
bool_t sigWriteRecord(sigWriteCallback_t writeCallback, void *context,
                      const sigRecord_t *record) {
  uint8_t header[SIG_HEADER_SIZE_BYTES];
  uint32_t edgesSize;
  uint32_t payloadCrc = 0xFFFFFFFFUL;
  uint32_t headerCrc;
  uint32_t dataOffset = SIG_HEADER_SIZE_BYTES;
  uint32_t metaOffset;

  if (writeCallback == NULL || !sigValidateWriteRecord(record)) {
    return FALSE;
  }
  if (!sigComputeEdgesSize(record, &edgesSize)) {
    return FALSE;
  }

  metaOffset = dataOffset + edgesSize;

  memset(header, 0, sizeof(header));
  memcpy(header, SIG_MAGIC, 4U);
  header[4] = record->signalType;
  header[5] = record->flags;
  header[6] = (uint8_t)record->tickScale;
  header[7] = 0U;
  sigWriteLe32(&header[8], record->edgeCount);
  sigWriteLe32(&header[12], dataOffset);
  sigWriteLe32(&header[16], (record->metadataSize > 0U) ? metaOffset : 0U);
  sigWriteLe32(&header[20], record->metadataSize);

  for (uint32_t i = 0; i < record->edgeCount; i++) {
    uint8_t raw[6];
    uint32_t rawSize;
    if (record->edges[i] <= 0xFFFEUL) {
      sigWriteLe16(raw, (uint16_t)record->edges[i]);
      rawSize = 2U;
    } else {
      sigWriteLe16(raw, SIG_EDGE_EXTENDED_16);
      sigWriteLe32(&raw[2], record->edges[i]);
      rawSize = 6U;
    }
    payloadCrc = sigCrc32Update(payloadCrc, raw, rawSize);
  }

  if (record->metadataSize > 0U) {
    payloadCrc =
        sigCrc32Update(payloadCrc, record->metadata, record->metadataSize);
  }
  payloadCrc ^= 0xFFFFFFFFUL;
  sigWriteLe32(&header[24], payloadCrc);

  sigWriteLe32(&header[28], 0U);
  headerCrc = sigCrc32(header, SIG_HEADER_SIZE_BYTES);
  sigWriteLe32(&header[28], headerCrc);

  if (!writeCallback(context, header, sizeof(header))) {
    return FALSE;
  }

  for (uint32_t i = 0; i < record->edgeCount; i++) {
    if (!sigWriteEdge(writeCallback, context, record->edges[i])) {
      return FALSE;
    }
  }

  if (record->metadataSize > 0U &&
      !writeCallback(context, record->metadata, record->metadataSize)) {
    return FALSE;
  }

  return TRUE;
}

/// Valida header .sig serializado.
static bool_t sigValidateHeader(const uint8_t *header, uint32_t *edgeCount,
                                uint32_t *dataOffset, uint32_t *metaOffset,
                                uint32_t *metaSize, uint32_t *payloadCrc) {
  uint8_t headerForCrc[SIG_HEADER_SIZE_BYTES];
  uint32_t storedHeaderCrc;
  uint32_t calcHeaderCrc;

  if (header == NULL || edgeCount == NULL || dataOffset == NULL ||
      metaOffset == NULL || metaSize == NULL || payloadCrc == NULL) {
    return FALSE;
  }

  if (memcmp(header, SIG_MAGIC, 4U) != 0) {
    return FALSE;
  }

  memcpy(headerForCrc, header, sizeof(headerForCrc));
  storedHeaderCrc = sigReadLe32(&headerForCrc[28]);
  sigWriteLe32(&headerForCrc[28], 0U);
  calcHeaderCrc = sigCrc32(headerForCrc, sizeof(headerForCrc));
  if (storedHeaderCrc != calcHeaderCrc) {
    return FALSE;
  }

  *edgeCount = sigReadLe32(&header[8]);
  *dataOffset = sigReadLe32(&header[12]);
  *metaOffset = sigReadLe32(&header[16]);
  *metaSize = sigReadLe32(&header[20]);
  *payloadCrc = sigReadLe32(&header[24]);

  if (*dataOffset != SIG_HEADER_SIZE_BYTES) {
    return FALSE;
  }
  if (*metaSize == 0U && *metaOffset != 0U) {
    return FALSE;
  }
  if (*metaSize > 0U && *metaOffset < *dataOffset) {
    return FALSE;
  }

  return TRUE;
}

/// Lee un registro .sig usando callbacks genéricos.
bool_t sigReadRecord(sigReadCallback_t readCallback, void *context,
                     sigRecordBuffer_t *record) {
  uint8_t header[SIG_HEADER_SIZE_BYTES];
  uint32_t edgeCount;
  uint32_t dataOffset;
  uint32_t metaOffset;
  uint32_t metaSize;
  uint32_t storedPayloadCrc;
  uint32_t payloadCrc = 0xFFFFFFFFUL;
  uint32_t encodedEdgesSize = 0U;

  if (readCallback == NULL || record == NULL) {
    return FALSE;
  }
  if (!readCallback(context, header, sizeof(header))) {
    return FALSE;
  }
  if (!sigValidateHeader(header, &edgeCount, &dataOffset, &metaOffset,
                         &metaSize, &storedPayloadCrc)) {
    return FALSE;
  }
  if (header[4] != SIG_SIGNAL_TYPE_IR && header[4] != SIG_SIGNAL_TYPE_RF) {
    return FALSE;
  }

  if (edgeCount > record->edgesCapacity) {
    return FALSE;
  }
  if (metaSize > record->metadataCapacity) {
    return FALSE;
  }
  if (edgeCount > 0U && record->edges == NULL) {
    return FALSE;
  }
  if (metaSize > 0U && record->metadata == NULL) {
    return FALSE;
  }

  for (uint32_t i = 0; i < edgeCount; i++) {
    uint8_t raw[6];
    uint32_t rawSize;
    uint32_t ticks;

    if (!sigReadEdge(readCallback, context, &ticks, raw, &rawSize)) {
      return FALSE;
    }
    record->edges[i] = ticks;
    payloadCrc = sigCrc32Update(payloadCrc, raw, rawSize);
    encodedEdgesSize += rawSize;
  }

  if (metaSize > 0U) {
    if (metaOffset != (dataOffset + encodedEdgesSize)) {
      return FALSE;
    }
  } else if (metaOffset != 0U) {
    return FALSE;
  }

  if (metaSize > 0U) {
    if (!readCallback(context, record->metadata, metaSize)) {
      return FALSE;
    }
    payloadCrc = sigCrc32Update(payloadCrc, record->metadata, metaSize);
  }

  payloadCrc ^= 0xFFFFFFFFUL;
  if (payloadCrc != storedPayloadCrc) {
    return FALSE;
  }

  record->signalType = header[4];
  record->flags = header[5];
  record->tickScale = (int8_t)header[6];
  record->edgeCount = edgeCount;
  record->metadataSize = metaSize;

  return TRUE;
}

/// Agrega un campo metadata codificado como TLV: type(1), len(1), value(len).
bool_t sigMetadataAppendTlv(uint8_t *buffer, uint32_t bufferCapacity,
                            uint32_t *currentSize, uint8_t type,
                            const uint8_t *value, uint8_t valueLength) {
  uint32_t required;

  if (buffer == NULL || currentSize == NULL) {
    return FALSE;
  }
  if (valueLength > 0U && value == NULL) {
    return FALSE;
  }

  required = 2U + (uint32_t)valueLength;
  if (*currentSize > bufferCapacity || (bufferCapacity - *currentSize) < required) {
    return FALSE;
  }

  buffer[*currentSize] = type;
  buffer[*currentSize + 1U] = valueLength;
  if (valueLength > 0U) {
    memcpy(&buffer[*currentSize + 2U], value, valueLength);
  }
  *currentSize += required;

  return TRUE;
}
