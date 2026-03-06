#import "../utils/units.typ": *

== Formato de archivo `.sig` <ap-formato-sig>

El dispositivo guarda las señales capturadas en archivos con extensión `.sig`, utilizando un formato binario propio orientado a:

- almacenar secuencias temporales de IR y RF en poco espacio,
- mantener compatibilidad entre captura y replay,
- incorporar verificación de integridad con CRC.

La implementación de referencia está en `program/src/utils/sig.c` y su interfaz en `program/inc/utils/sig.h`.

=== Estructura general

Cada archivo se compone de:

1. Header fijo de 32 bytes.
2. Bloque de duraciones (_edges_) codificado en longitud variable.
3. Bloque opcional de metadata en formato TLV.

==== Header (32 bytes)

#figure(
  table(
    columns: (auto, auto, auto),
    align: (left, left, left),
    table.header[*Offset*][*Campo*][*Descripción*],
    [0..3], [`magic`], [`SIG1`],
    [4], [`signalType`], [Tipo de señal: `1` IR, `2` RF.],
    [5], [`flags`], [Bits de estado (`START_LEVEL`, `HAS_METADATA`, `HAS_PROFILE`).],
    [6], [`tickScale`], [Escala temporal (en esta versión se usa `-6`, equivalente a #us[1] por tick).],
    [7], [Reservado], [Actualmente en 0.],
    [8..11], [`edgeCount`], [Cantidad de duraciones serializadas.],
    [12..15], [`dataOffset`], [Inicio del bloque de _edges_ (debe ser 32).],
    [16..19], [`metaOffset`], [Inicio de metadata (0 si no hay metadata).],
    [20..23], [`metaSize`], [Tamaño total de metadata en bytes.],
    [24..27], [`payloadCrc32`], [CRC32 de _edges_ codificados + metadata.],
    [28..31], [`headerCrc32`], [CRC32 del header con este campo forzado a 0 para el cálculo.],
  ),
  caption: [Campos del header del formato `.sig`.],
)

==== Codificación de duraciones (_edges_)

Cada duración se serializa en little-endian con dos variantes:

- Si `ticks <= 0xFFFE`: se guarda en 2 bytes (`uint16`).
- Si `ticks > 0xFFFE`: se usa secuencia extendida de 6 bytes: marcador `0xFFFF` + valor real en `uint32`.

Esta estrategia reduce tamaño para pulsos cortos, manteniendo soporte para duraciones largas cuando aparecen.

==== Metadata opcional (TLV)

Cuando existe metadata, se guarda al final del payload como secuencia TLV (`type`, `len`, `value`), donde:

- `type`: 1 byte.
- `len`: 1 byte.
- `value`: `len` bytes.

Tipos utilizados actualmente:

- `1` (`SIG_META_NEC_ADDR`): dirección NEC (1 byte).
- `2` (`SIG_META_NEC_CMD`): comando NEC (1 byte).
- `16` (`SIG_META_RF_FREQ_HZ`): frecuencia RF en Hz (4 bytes, little-endian).
- `17` (`SIG_META_RF_MODULATION`): modulación RF (1 byte: AM270 o AM650).

==== Verificación de integridad

El formato usa CRC32 IEEE (polinomio `0xEDB88320`) en dos niveles:

- `headerCrc32`: protege el header completo.
- `payloadCrc32`: protege el bloque de datos (_edges_ codificados + metadata).

Durante la lectura se verifica primero el header y luego el payload. Si alguno falla, el archivo se descarta.

=== Script de inspección

El script `scripts/custom/signals.py` permite analizar archivos `.sig` fuera del firmware. Entre otras funciones:

- valida `headerCrc32` y `payloadCrc32`,
- parsea metadata TLV y decodifica campos conocidos,
- reconstruye y grafica la forma de onda temporal para inspección visual.
