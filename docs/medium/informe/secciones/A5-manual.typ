#import "../utils/units.typ": *

== Manual de usuario <ap-manual>

Para utilizar _Médium_ se requiere:

- dos pilas AA cargadas (o la alimentación definida para la placa),
- tarjeta microSD insertada y formateada en FAT32.

Al encender, si la microSD no está disponible, el equipo muestra el mensaje _Insertá la microSD_ y no habilita las acciones de captura/replay.

=== Controles del equipo

La interacción se realiza con:

- _joystick_ (ejes X/Y): navegación por menús y acciones,
- botón `ENTER`: confirmar opción,
- botón `BACK`: volver/cancelar.

La pantalla principal permite elegir modo: *IR* o *RF*. Luego se ingresa al menú de _slots_ del modo seleccionado.

=== Organización de archivos en microSD

_Médium_ trabaja con slots fijos:

- `IR1.sig` a `IR5.sig` para infrarrojo,
- `RF1.sig` a `RF5.sig` para radiofrecuencia.

Cada slot puede estar vacío (acción disponible: grabar) u ocupado (acciones disponibles: reproducir o borrar).

=== Flujo de uso en el dispositivo

==== Grabar una señal IR

1. En la pantalla principal, seleccionar `IR` y confirmar.
2. Elegir un slot vacío.
3. Seleccionar la acción de _grabar_.
4. Apuntar al emisor IR hacia el receptor y emitir la señal.
5. Para cancelar, presionar `BACK`.

Si la captura es válida y la SD está disponible, el archivo se guarda en el slot elegido.

==== Grabar una señal RF (CC1101)

1. En la pantalla principal, seleccionar `RF` y confirmar.
2. Elegir un slot vacío y luego la acción de _grabar_.
3. Configurar los parámetros de captura:
  - Frecuencia: #MHz(433.920) o #MHz(315.000).
  - Modulación: `AM650` o `AM270`.
4. Confirmar y emitir la señal RF a capturar.
5. Para cancelar, presionar `BACK`.

Durante el guardado se almacena, además de la forma de onda, la metadata de RF (frecuencia y modulación), que luego se reutiliza en la reproducción.

==== Reproducir una señal

1. Seleccionar modo (`IR` o `RF`).
2. Ingresar al listado de slots.
3. Elegir un slot ocupado.
4. Seleccionar acción _play_.

En RF, si el `.sig` contiene metadata, el equipo reaplica automáticamente la frecuencia y la modulación guardadas antes de emitir.

==== Borrar una señal

1. Seleccionar modo.
2. Elegir slot ocupado.
3. Seleccionar acción _delete_.

Esta operación elimina el archivo `.sig` del slot y no se puede deshacer.

=== Recomendaciones de uso

- Mantener la microSD insertada durante toda la operación.
- En IR, ubicar emisor y receptor enfrentados y a corta distancia para mejorar la captura.
- En RF, evitar fuentes de ruido cercanas y respetar la frecuencia esperada del dispositivo objetivo.
- Si una captura falla, repetirla ajustando distancia/orientación o configuración RF.

=== Scripts de PC para desarrollo y análisis

En `scripts/custom` se incluyen herramientas para trabajar con el dispositivo desde una PC.

==== Preparación del entorno

1. Instalar `uv`.
2. Ejecutar:
  ```bash
  cd scripts/custom
  uv sync
  ```

==== Terminal serie (`terminal.py`)

Permite ver logs UART y enviar texto a la placa.

```bash
cd scripts/custom
uv run terminal.py --list-ports
uv run terminal.py
```

Opcionalmente se puede fijar puerto/baudrate:

```bash
uv run terminal.py /dev/ttyUSB1 --baudrate 115200
```

==== Inspección de archivos `.sig` (`signals.py`)

Permite validar CRC, parsear metadata y visualizar la forma de onda.

```bash
cd scripts/custom
uv run signals.py RF1.sig
```

Para exportar imagen sin abrir ventana interactiva:

```bash
uv run signals.py RF1.sig --save RF1.png
```

=== Solución rápida de problemas

- _Mensaje "Insertá la microSD" permanente_: verificar formato FAT32, contactos de la tarjeta e inserción completa.
- _No guarda en slot_: revisar que la SD siga montada y que el slot seleccionado corresponda al modo actual.
- _No reproduce en RF_: confirmar frecuencia/modulación esperada del receptor objetivo.
- _Sin logs UART en PC_: revisar puerto serie, permisos de sistema y baudrate #Hz(115200).
