#import "../utils/units.typ": *

= Diseño del firmware

Se desarrolló un firmware trabajar con todos los módulos presentados. El mismo se encuentra publicado en #link("https://github.com/santi-nihany/medium").

== Arquitectura general

El sistema corre sobre FreeRTOS en modo _preemptive_, con una arquitectura simple basada en dos tareas principales: `Storage` y `UI`. La captura y reproducción IR/RF se ejecuta desde la lógica de la tarea de interfaz, mientras que la tarea de almacenamiento se encarga del monitoreo periódico de la microSD. Para evitar conflictos en periféricos compartidos, el acceso al bus SPI (microSD + CC1101) se arbitra con un mutex global estático. Desde la interfaz, el usuario puede grabar, reproducir y borrar señales en slots fijos.

=== Diagrama de flujo

#figure(
  image("../imagenes/diagrama-flujo-ui.jpg"),
  caption: [Diagrama de flujo de la interfaz de usuario.],
) <fig-flujo-ui>

Más detalle sobre cómo el usuario interactúa con el sistema en el @ap-manual.

#figure(
  ```
  1. Programa principal
     - Configura el hardware (timers, GPIOs, SD, UART).
     - Inicializa módulos (`buttons`, `display`, `storage`, `ir`, `rf`) y el mutex SPI.
     - Crea dos tareas: `Storage` y `UI`.
     - Arranca el scheduler → el RTOS empieza a planificar tareas.

  2. Task `Storage` (prioridad media-alta)
     - Ejecuta periódicamente `storageUpdate()`.
     - Detecta cambios de estado de la microSD (insertada/no disponible).
     - Realiza un `probe` periódico del filesystem para detectar extracción sin pin CD.

  3. Task `UI` (prioridad media)
     - Lee joystick y botones (ENTER/BACK).
     - Gestiona el menú en LCD.
     - Para grabación: ejecuta captura IR o RF, serializa a formato `.sig` y guarda en microSD.
     - Para reproducción: carga `.sig` desde microSD y llama a `irReplayEdges()` o `rfReplayEdges()`.
     - Para borrado: elimina el archivo asociado al slot.

  4. Módulos IR/RF
     - Implementan captura y replay con cancelación desde callback de UI.
     - En RF se permite seleccionar frecuencia/modulación antes de grabar y se persiste metadata en el `.sig`.

  5. Almacenamiento
     - Se usa FatFS para lectura/escritura de archivos `.sig`.
     - El acceso SPI está protegido por mutex para coexistencia SD/CC1101.
     - Los slots son fijos por modo: `IR1.sig` a `IR5.sig` y `RF1.sig` a `RF5.sig`.

  ```,
  caption: [Pseudocódigo del diagrama de la @fig-flujo-ui.],
)

=== Bibliotecas externas y dependencias

Bibliotecas del proyecto CIAA
- sAPI v0.6.2

// === Bibliotecas del fabricante / SoC

// - *CMSIS Core* — interfaz ARM Cortex.
// - *LPC43xx HAL / BSP / drivers* — periféricos específicos del MCU.
// - *lpc_open* — drivers/ejemplos si se usa LPC4337 (según disponibilidad en tu árbol).

Librerías de terceros
- FreeRTOS v10 --- kernel RTOS preemptivo.
- FatFS --- sistema de archivos para microSD (`f_write`, `f_open`, `f_close`).

== Módulos desarrollados

=== Sistema RTOS

Se implementó un esquema RTOS compacto con dos tareas de aplicación: `Storage` y `UI`. A diferencia de versiones anteriores, en la versión actual no se utiliza una arquitectura por colas/stream buffers entre tareas para IR/RF; las operaciones de captura, serialización y replay se ejecutan dentro de la tarea de UI, y la tarea de almacenamiento queda dedicada al estado de la microSD.

==== Primitivas de RTOS creadas

Se utilizan las siguientes primitivas de FreeRTOS:

- *Tasks*:
  - `Storage` (prioridad `tskIDLE_PRIORITY + 3`), período base de #ms(20), con _probe_ de filesystem cada #ms(100).
  - `UI` (prioridad `tskIDLE_PRIORITY + 2`), lazo principal de interacción cada #ms(40).
- *Mutex estático del bus SPI*: arbitra el acceso concurrente entre microSD y transceptor CC1101.
- *Delays y ticks de scheduler*: usados para temporización de interfaz, debounce y ventanas mínimas de visualización.

==== Pruebas

Como primera verificación, se comprobó por UART el arranque correcto del _scheduler_ y la creación de las tareas `Storage` y `UI`. También se validó el comportamiento esperado por prioridad: la tarea de almacenamiento mantiene el monitoreo periódico de SD, y la UI conserva la respuesta del menú con ciclos de #ms(40) sin bloquear el sistema.

Como segunda prueba, se validó el flujo completo de usuario en ambos modos (IR y RF): captura con posibilidad de cancelación, guardado en archivo `.sig`, reproducción desde slot y borrado. En RF, además, se verificó la selección previa de frecuencia/modulación y su persistencia como metadata TLV dentro del archivo.

Se verificó por UART que:

+ El estado de la microSD cambia correctamente entre "lista" y "no disponible".
+ Las operaciones de guardado/carga de `.sig` finalizan correctamente para IR y RF.
+ El sistema detecta cancelación de captura por botón o por retiro de SD sin quedar bloqueado.
+ La reproducción respeta el tipo de señal del archivo y aplica metadata RF cuando está disponible.

=== Pantalla OLED

Para la pantalla OLED con _driver_ SH1106 se desarrollaron dos librerías. La primera, `sh1106.h` se encarga de la comunicación con la pantalla en sí utilizando I#super[2]C y encapsula todas las particularidades del dispositivo. La segunda, `display.h` es una librería de más alto nivel que consume `sh1106.h` y expone funciones para dibujar líneas, rectángulos, pegar imágenes y demás.

Para minimizar el uso de memoria utilizada, se guardan distintos _sprites_ en la memoria de programa. Además, también se cuenta con la capacidad de escribir texto arbitrario con una tipografía predefinida.

En la @fig-display-menu se muestra la pantalla mostrando el menú de inicio del programa.

#figure(
  image("../imagenes/foto-display-menu.jpg", width: 40%),
  caption: [Pantalla OLED mostrando el menú de inicio.],
) <fig-display-menu>

=== MicroSD

Se integró FatFS con el sistema RTOS descrito en la sección anterior. En la versión actual, los archivos se gestionan por slots fijos (`IR1.sig` ... `IR5.sig` y `RF1.sig` ... `RF5.sig`). La tarea `UI` solicita guardado/carga/borrado según la acción elegida por el usuario, y el módulo `storage` ejecuta I/O sobre FatFS con exclusión mutua del bus SPI para convivir con el CC1101. Se verificó escritura y lectura exitosa en ambos modos, incluyendo metadata de RF (frecuencia y modulación) dentro del formato `.sig`.

Además, para serializar las capturas se definió un formato binario propio `.sig`, implementado en `program/src/utils/sig.c`. Como herramienta de desarrollo y depuración se agregó también el script `scripts/custom/signals.py`, que permite validar e inspeccionar los archivos generados por firmware desde PC. En el @ap-formato-sig se detalla la estructura del formato.

En la @fig-logs-rtos-sd se observa la salida por UART durante la ejecución del sistema, donde se evidencia el monitoreo de SD y las operaciones de guardado/carga en la microSD.

#figure(
  image("../imagenes/logs-rtos-sd.jpeg", width: 40%),
  caption: [Logs de monitoreo RTOS y operaciones de almacenamiento en microSD.],
) <fig-logs-rtos-sd>


=== Modulo IR

// ==== ¿Que es una señal Infrarroja?
En los sistemas de comunicación por infrarrojo, la información digital no se transmite sobre una señal infrarroja continua, sino que como una señal modulada sobre una portadora de alta frecuencia. En la gran mayoría de controles comerciales esta portadora es aproximadamente de #kHz(38), por eso se decidió hacer que el modulo detecte/emita señales en esta frecuencia.

La utilización de una portadora presenta varias ventajas:
- Permite a los receptores IR comerciales (por ejemplo, módulos tipo TSOP, KY-022, VS1838B) filtrar la luz ambiente y el ruido proveniente de fuentes como lámparas o el sol.
- Simplifica la detección de la señal útil, ya que el receptor entrega una salida digital ya demodulada.
- Aumenta la robustez del sistema frente a interferencias.

Desde el punto de vista lógico, *el receptor no entrega la portadora, sino una señal digital* que indica presencia o ausencia de trenes de #kHz(38).

La información binaria (bits 0 y 1) se codifica mediante la duración de los pulsos infrarrojos, utilizando combinaciones de:
- `MARK`: período en el que la portadora de #kHz(38) está presente (LED IR encendido).
- `SPACE`: período en el que no hay emisión infrarroja (LED apagado).
La diferencia entre un 0 y un 1 no se da por el nivel lógico, sino por la duración del espacio posterior al pulso activo.

Existen múltiples protocolos de transmisión infrarroja, desarrollados por distintos fabricantes y estandarizados de hecho en la industria. Algunos de los más conocidos son:
- NEC
- RC-5 / RC-6 (Philips)
- Sony SIRC
- Panasonic

Cada protocolo define la frecuencia de la portadora, estructura temporal de la trama, la forma en la que se codifican los bits, el tamaño de mensaje etc. Esto implica que no existe una única forma universal de decodificar señales IR, sino que el software debe estar diseñado para un protocolo específico o ser lo suficientemente flexible para soportar varios.

==== NEC
El protocolo NEC es uno de los más utilizados y documentados, tanto en dispositivos comerciales como en proyectos educativos y académicos. Su popularidad se debe a su simplicidad, claridad temporal y bajo costo de implementación.

La estructura típica de la trama se detalla en la @fig-trama-nec.

#figure(
  image("../imagenes/diagrama-trama-nec.png"),
  caption: [Trama NEC.],
) <fig-trama-nec>

Se puede apreciar que la trama tiene un pulso de encabezado que da inicio (_header_) que consta de #ms(9) de `MARK` y #ms(4.5) de `SPACE`. Luego continua con 32 bits que se dividen en dos, 16 para mandar el address del emisor y 16 para el comando que se desea compartir. Para llevar un control de errores de los 16 bits, 8 son útiles para transportar datos y 8 son para control de errores ya que enviá la misma señal pero invertida.

La codificación de bits es la de la @tabla-nac.

#figure(
  table(
    columns: 3,
    [], [0], [1],
    `MARK`, us(560), us(560),
    `SPACE`, us(560), us(1690),
  ),
  caption: [Codificación de bits NAC (valores aproximados).],
) <tabla-nac>

==== Software

El diseño del software se centró en la implementación de un sistema capaz de recibir, decodificar y transmitir señales infrarrojas bajo el protocolo NEC, priorizando la robustez temporal y la facilidad de depuración, utilizando una estrategia de polling temporizado.

Esta decisión se tomó luego de evaluar el comportamiento del sistema bajo distintos enfoques y observar interferencias temporales al utilizar múltiples interrupciones simultáneas.

#underline[Arquitectura general del sistema]

A nivel funcional, el software se divide en los siguientes bloques lógicos:

1. Adquisición de señal infrarroja (RX -- Polling)
2. Procesamiento y decodificación del protocolo NEC
3. Generación de señal infrarroja (TX -- Emisión NEC)
4. Gestión temporal mediante temporizadores
5. Programa principal (control de flujo y validación)

===== Adquisición de señal infrarroja (RX)

La recepción de la señal infrarroja se implementó mediante polling periódico del pin GPIO7, con un intervalo de muestreo fijo (SAMPLE_US). Esta técnica consiste en leer el estado lógico del pin a intervalos regulares y detectar transiciones de nivel comparando el estado actual con el anterior. Para medir la duración de cada pulso, se utiliza un temporizador de hardware configurado para contar microsegundos. Cada vez que se detecta un cambio de nivel en la entrada IR se hace lo siguiente:
- Se calcula el tiempo transcurrido desde el ultimo cambio.
- Se almacena el nivel lógico previo y su duración
- Se filtran pulsos demasiado cortos, considerados ruido o glitches.

Los pulsos válidos se almacenan en un buffer de estructuras ```c IRPulse_t```, que contiene dos campos:  Nivel logico del pulso (MARK/SPACE) y la duracion en microsegundos.

El fin de trama se detecta cuando ya se han capturado almenos algunos pulso validos o el tiempo sin cambio en la entrada supera el TIMEOUT (definido como constante). Este criterio es consistente con el protocolo NEC, donde existe un silencio prolongado entre tramas.

===== Procesamiento y decodificación del protocolo NEC

La decodificación se realiza sobre el buffer de pulsos capturados, interpretando las duraciones conforme a la especificación del protocolo NEC. Es oportuno aclarar que el modulo KY-022 entrega la inversa de la señal emitida, es decir que constantemente esta en valor ALTO y cuando hay presencia de portadora su valor entregado pasa a estado BAJO.

Las etapas del algoritmo de decodificación son las siguientes:

- Validación de encabezado: Pulso bajo de aproximadamente 9ms y pulso alto de 4.5ms
- Decodificación de bits: cada bit se compone de un pulso bajo fijo de 560us y un pulso alto cuya duración determina si el bit es 0 o 1.
- Reconstrucción de la palabra de datos: Se reconstruyen 32 bits enviados LSB primero y se separan los campos.
- Verificación de integridad: Se comprueba que cada byte coincida con el complemento de su inverso. Si la verificación falla, la trama se descarta.

Este enfoque permite asegurar que únicamente se acepten tramas NEC válidas, descartando señales corruptas o protocolos incompatibles.

===== Generación de señal infrarroja (TX -- Emisión NEC)

La transmisión de señales infrarrojas se implementó en dos niveles, como base la generación de la portadora por software alternando el estado del pin de salida a intervalos equivalentes a medio período de la frecuencia deseada (≈ #us(13) para #kHz(38)).Y por complemento la emisión de la trama siguiendo estrictamente la estructura del protocolo NEC.

Para garantizar la precisión temporal durante la transmisión, se utiliza un enfoque bloqueante, deshabilitando interrupciones durante la emisión completa de la trama. Esto asegura que:

- No haya jitter introducido por otras tareas.

- Las duraciones de MARK y SPACE se mantengan dentro de los márgenes del protocolo.

===== Generación temporal

El temporizador `TIMER2` se configura como un contador libre en microsegundos y se utiliza como base de tiempo global del sistema. Su funcion principal es:

- Medir la duración de pulsos infrarrojos recibidos.

- Detectar flancos de señal en el receptor IR.

- Determinar el fin de una trama por inactividad.

- Controlar retardos precisos durante la transmisión.

La función ```c delayInaccurateUs()``` se utiliza exclusivamente durante la captura de señal por polling, actuando como un retardo de muestreo.

El *TIMER0* se utiliza para generar la portadora infrarroja de forma periódica, alternando el estado del pin de salida a intervalos equivalentes a medio período de la portadora. TIMER0 se configura para generar eventos que invierten el estado logico del pin GPIO5 cada ~#us(13).

El *TIMER3* se emplea para controlar la máquina de estados de transmisión NEC. Dispara callbacks al cumplirse la duración de cada segmento del protocolo.
En cada callback se decide el próximo estado y la duración del siguiente intervalo.


===== Programa Principal

El programa principal se encarga de:

- Inicializar periféricos (GPIO5/7, UART, temporizador).

- Esperar la llegada de una señal infrarroja.

- Capturar y almacenar los pulsos recibidos.

- Decodificar la trama NEC.

- Mostrar los resultados por consola serie.

- Reemitir la trama decodificada como verificación funcional.

Este flujo permite validar el sistema de punta a punta, utilizando tanto un control remoto comercial como un receptor externo para confirmar la correcta transmisión.
