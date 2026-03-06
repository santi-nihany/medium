#import "../utils/units.typ": *
#import "../utils/draw_signal.typ" as sig

= Ensayos y mediciones

Para validar el correcto funcionamiento de _Médium_ se realizaron distintos ensayos. Esta sección se centrará en los ensayos con los módulos más complejos (IR y RF).

== Módulo de infrarrojos (IR)

=== Funcionamiento correcto de transmisión y recepción

En este escenario se validó el funcionamiento completo del sistema de comunicación infrarroja, verificando tanto la recepción como la reemisión de tramas NEC entre dos plataformas distintas.

Se configuro un Arduino como emisor de una trama infrarroja fija usando la librería IRremote.hpp. La trama emitida fue:
- Address: 0xA0
- Command: 0xE3

Una vez validada la trama recibida, la EDU-CIAA reemitió exactamente la misma trama infrarroja.

Finalmente, el Arduino decodificó la señal retransmitida por la EDU-CIAA y se verifico que sea la misma.

#figure(
  image("../imagenes/ensayo-ir1.png", height: 40%),
  caption: [Escenario de prueba.],
) <fig-ensayo-ir1>

#grid(
  columns: 2,
  gutter: 1em,
  [
    #figure(
      image("../imagenes/ensayo-ir1-arduino-send.png"),
      caption: [Emisión desde Arduino.],
    ) <fig-ensayo-ir1-arduino-send>
  ],
  [
    #figure(
      image("../imagenes/ensayo-ir1-educiaa-recieve.jpeg", height: 10cm),
      caption: [Recepcion en la EDU-CIAA.],
    ) <fig-ensayo-ir1-educiaa-recieve>
  ],

  [
    #figure(
      image("../imagenes/ensayo-ir1-educiaa-recieve2.jpeg"),
      caption: [Recepcion en la EDU-CIAA.],
    ) <fig-ensayo-ir1-educiaa-recieve2>
  ],
  [
    #figure(
      image("../imagenes/ensayo-ir1-arduino-recieve.png"),
      caption: [Recepcion en Arduino.],
    ) <fig-ensayo-ir1-arduino-recieve>
  ],
)

Este ensayo confirma que:

- La implementación del decodificador NEC es correcta.

- La generación temporal de la trama NEC en la EDU-CIAA cumple con las tolerancias del protocolo.

- El sistema es interoperable entre distintas plataformas de hardware.

Este escenario valida el caso de uso principal del proyecto, demostrando un flujo completo.

=== Implementación alternativa con interrupciones y descarte de la solución

En una etapa inicial del desarrollo se implementó una versión del receptor infrarrojo basada en interrupciones por cambio de estado en el pin GPIO7, con el objetivo de detectar flancos de señal de forma inmediata.
Descripción de la implementación

- Se configuró una interrupción por flanco en GPIO7.

- Cada cambio de nivel generaba una interrupción para medir la duración del pulso.

- El esquema funcionó correctamente en condiciones controladas, con solo el módulo receptor IR conectado a la EDU-CIAA.

#figure(
  image("../imagenes/ensayo-ir2.png", height: 20%),
  caption: [Escenario interrupcion por flanco.],
) <fig-ensayo-ir2>

El problema surgio al integrar el sistema completo y conectar el shield con los demas modulos, se observo:
- Incremento significativo de ruido eléctrico en la línea de entrada.
- Generación de múltiples interrupciones espurias.

- Saturación del sistema de interrupciones.

- Bloqueo del programa principal debido a la alta frecuencia de eventos.

Este comportamiento provocó que el sistema dejara de responder correctamente, incluso ante señales válidas. Y como esta solución tenia que ser funcional en conjunto con los demás módulos, se decidió descartar la solución basada en interrupciones y adoptar un enfoque de polling.

=== Alimentación

Durante la etapa de ensayos del módulo infrarrojo se evaluó el comportamiento del sistema bajo distintas condiciones de alimentación eléctrica, con el objetivo de verificar su funcionamiento en escenarios reales de uso.

En una primera instancia, los ensayos se realizaron alimentando el sistema mediante un banco de pilas, cuya tensión medida fue de aproximadamente #V(6.8). Bajo estas condiciones, el módulo receptor infrarrojo KY-022 funcionó correctamente, permitiendo la detección de señales y la decodificación de tramas sin inconvenientes.

Sin embargo, al reemplazar el banco de pilas por un transformador de #V(9) para alimentar el shield completo, se observó un comportamiento anómalo: el pin GPIO7 de la EDU-CIAA permanecía en un estado fijo y no detectaba ningún cambio de nivel lógico, aun cuando el módulo IR estaba recibiendo señales válidas.

Ante este escenario se intuyo que el problema venia del hardware y la alimentación. El modulo en arduino era funcional con #V(3.3) y con #V(5). Entonces se consulto la bibliografia de la EDU-CIAA. A partir de este análisis se identificó que los pines GPIO de la EDU-CIAA operan en niveles lógicos de #V(0) a #V(3.3) y se determino que el problema venia desde el diseño del esquematico.

Para resolver esta incompatibilidad de niveles de tensión, se realizó una modificación en el shield:

- Se interrumpió la pista original que conectaba el pin VCC del módulo KY-022 a la línea de #V(5), utilizando un corte físico (cuter).

- Se realizó un puente entre el pin VCC del módulo IR y la línea de #V(3.3), proveniente del regulador asociado al diodo Zener de la placa.


Luego de aplicar esta modificación:
- El sistema funcionó correctamente tanto con el banco de pilas como con el transformador de #V(9).
- El pin GPIO7 volvió a detectar correctamente los cambios de nivel.
- La decodificación y retransmisión de tramas infrarrojas se realizó sin errores.

== Módulo de radiofrecuencia (RF)

=== Validación mínima de funcionamiento

Utilizando el módulo CC1101 y la librería `sapi.h` se realizó una comunicación básica con el transmisor FS1000A y el receptor MX-RM-5v. Se configuró el módulo RF para comunicarse en la frecuencia #MHz(433) con modulación OOK.

Para la prueba de transmisión se utilizó la comunicación UART para elegir cuando transmitir un "1" o un "0" desde la terminal. En la @fig-ensayo-rf0 se observa la prueba del modulo RF como transmisor, en la foto se enciende la portadora y el receptor MX-RM-5v tiene un "1" en la salida, que se traduce en el led de salida encendido.

#figure(
  image("../imagenes/ensayo-rf0.png", height: 30%),
  caption: [Prueba de modulo RF como transmisor.],
) <fig-ensayo-rf0>

Para la prueba de receptor se utilizó la comunicación UART para imprimir en la terminal continuamente el valor del pin `GDO2` del módulo RF, este estaba configurado para indicar de manera asincronía el valor recibido. La prueba fue exitosa ya que al transmitir o no transmitir con el módulo FS1000A se representaba correctamente la salida en la terminal.

=== Primer ensayo: captura de señal de #MHz(315)

#figure(
  table(
    columns: 2,
    align: left,
    table.header[*Propiedad*][*Valor*],
    [Modo], [recepción],
    [Modulación], [OOK],
    [Frecuencia], MHz(315),
    [Ancho de banda], kHz(812.5),
    [Formato], [serial asíncrono],
  ),
  caption: [Configuración del módulo CC1101 para el primer ensayo.],
)

Como objetivo se utilizó un timbre de RF _Candela 7279_ el cual cuenta con un parlante con LED (receptor) y un botón que al presionarlo hace sonar el parlante (emisor). Este aparato se comunica en una frecuencia de #MHz(315), y es una comunicación sencilla de modulación OOK.

#figure(
  image("../imagenes/foto-candela-7279.png", height: 20%),
  caption: [Timbre Candela 7279],
) <fig-candela-7279>

Para realizar la captura, el módulo este replica la señal que capta por la antena en el pin `GDO0`, por lo que se observó el valor del mismo repetidamente en intervalos regulares de tiempo para así obtener la forma de la señal que genera el emisor.

La forma de onda se presenta en la @fig-ensayo-rf1-captura (captura realizada con intervalo de #us(100)), se intentó replicar para "duplicar el emisor" con el objetivo de activar el receptor pero no funcionó. Esto es debido a que no se capturó correctamente la señal transmitida por el emisor, se puede observar en la forma de onda que no tiene ningún patrón aparente.

#figure(
  sig.draw-RF-signal(
    "0000000000000000000000178F1C7C003FFFC0707FFFE03CFC7F01E03FE1FFFE38FE7BFF0601E707EF703FE0C1F8C0FFC7BE3C7FC003F1FE0FFF81F107F80FC603FE79FFF11E01FC3FC2671E201F878FFDFF81FE3FFFC7F8387F87C1FFC03FFFF03F8038FE7FE1C03C7F380078C33031F3C0E01E180707EFC78E73F9FFFE1F000000000000000000000000000000001FE39E000003FFFF1F801FFFFFE7FEF07FF88FF0000000000000000000000000000000000000",
    name: [Captura timbre 1],
    bit-time: 100,
    bit-width: 0.58pt,
    bytes-per-line: 100,
    time-mark-interval: 10000,
  ),
  caption: [Forma de onda generada por el timbre Candela 7279.],
) <fig-ensayo-rf1-captura>


=== Segundo ensayo: captura de señal de #MHz(433.92)

#figure(
  table(
    columns: 2,
    align: left,
    table.header[*Propiedad*][*Valor*],
    [Modo], [recepción],
    [Modulación], [OOK],
    [Frecuencia], MHz(433.92),
    [Ancho de banda], kHz(101),
    [Formato], [serial asíncrono],
  ),
  caption: [Configuración del módulo CC1101 para el segundo ensayo.],
)

Como objetivo se utiliza un control remoto de portón electrico marca ZAP. Este cuenta en su interior con el chip `Si4010-C2` que cuenta con las siguientes características:

#figure(
  table(
    columns: 2,
    align: left,
    table.header[*Característica*][*Valor*],
    [Rango de frecuencia], [#MHz(27) - #MHz(960)],
    [Modulación], [FSK/OOK],
    [Tasa de símbolos], [Hasta #kbps(100)],
    [Codificación], [Código fijo y código rolante],
  ),
  caption: [Características del chip SI4010-C2.],
)

Este chip es aplicado en sistemas de:
- Controles de puertas y portones
- Llaves remotas sin cerradura
- Domotización y seguridad del hogar
- Controles remotos inalámbricos // posta dice así en el datasheet y me hace mucha gracia
\

En este caso es utilizado para un portón electrico en Argentina. La configuración mas común para estos casos es utilizar una frecuencia de #MHz(433.92), modulación OOK y codificación de código fijo.

#figure(
  image("../imagenes/foto-control-zap.png", height: 30%),
  caption: [Control remoto de portón electrico ZAP.],
) <fig-control-zap>

El método de codificación es lo mas importante en cuanto a seguridad. Si se utiliza código rolante cada vez que el control remoto se comunique con el portón se enviará un código distinto perteneciente a una lista de códigos que conocen ambos dispositivos, como cada vez se utiliza un código nuevo este no puede ser replicado.

Si se utiliza código fijo el emisor siempre transmite la misma señal, con el mismo código y secuencia de bits. Si la señal es capturada puede ser replicada y enviada al receptor en cualquier momento para que este realice la acción que tenga programada, la cual puede ser abrir un portón.

El fin de este ensayo es capturar y analizar la señal emitida por el control remoto.

Dado que no se conocía la frecuencia de operación del control remoto, se realizaron capturas iniciales comenzando en #MHz(433), frecuencia comúnmente utilizada en este tipo de dispositivos, con un ancho de banda de #kHz(812.50). Sin embargo, no se obtuvo información significativa.

Posteriormente, se modificó la configuración del módulo CC1101 para realizar capturas en #MHz(433.92), donde se logró detectar una señal, aunque con interferencias por ruido del entorno. En consecuencia, se redujo progresivamente el ancho de banda hasta alcanzar los #kHz(101), punto en el cual se obtuvo una señal clara.

#figure(
  sig.draw-RF-signal(
    "000fffffff0001fffffffe00000003ffffc0003fffffffc00000007fff80000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000007ffff800000000ffff800000000ffff0000ffffffff000000001ffffe000000003ffffc000000007ffff8000000007ffff800000001ffff000000003ffff000000003ffffc00003ffffffffc000000007ffff0000ffffffff000000001ffff000000001ffffc00007ffffffffe00007ffffffff800007ffffffff0000ffffffff00003ffffffffe00003ffffffffc00007ffffffff800000001ffff80001ffffffff00003ffffffffe000000003ffff800007ffffffffc00000000ffff800000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000007ffffc000000007ffff800000000ffff00001ffffffffe000000001ffffc000000003ffffc000000007ffff800000000ffff000000001ffff000000001ffffe000000003ffffc00003ffffffffc000000007fff00001ffff000000003ffffe00007ffffffffc0000ffffffff80000ffffffff00001ffffffff00003ffffffffe00003ffffffffc0000ffffffff8000000007ffff0000ffffffffe00001ffffffffe000000007ffffc00003ffffffffc00000000ffff8000000000000000000000000000000000",
    name: "test2",
    bit-time: 50,
    bit-width: 0.205pt,
    bytes-per-line: 280,
    time-mark-interval: 10000,
  ),
  caption: [Forma de onda generada por el control remoto de portón ZAP.],
) <fig-ensayo-rf2-captura>

La señal capturada se presentada en la figura @fig-ensayo-rf2-captura, en la cual se puede observar 2 bloques de datos idénticos transmitidos por el control remoto.

Esto indica que el control remoto utiliza _código fijo_, la misma señal que se transmite en el bloque de tiempo #us(30000) - #us(110000) se observa en el bloque de tiempo #us(120000) - #us(200000).

=== Tercer ensayo: ataque de interferencia (_jamming_)

#figure(
  table(
    columns: 2,
    align: left,
    table.header[*Propiedad*][*Valor*],
    [Modo], [transmisión],
    [Modulación], [OOK],
    [Frecuencia], MHz(315),
    [Ancho de banda], kHz(812.5),
    [Formato], [serial asíncrono],
  ),
  caption: [Configuración del módulo CC1101 para el tercer ensayo.],
)

El objetivo de este ensayo es interferir en la comunicación del timbre _Candela 7279_ para que al presionar el botón no se active el parlante.

El módulo CC1101, configurado en modo de transmisión serial asincrónica, replica en la portadora el estado lógico presente en el pin `GDO0`. Es decir, cuando `GDO0` se encuentra en nivel *BAJO* el módulo desactiva la portadora, en cambio, si se encuentra en nivel *ALTO* la portadora permanece activa.

Para este ensayo, el pin `GDO0` se mantiene en nivel *ALTO* de forma constante, forzando la transmisión continua en la misma frecuencia utilizada por el timbre, con el objetivo de generar interferencia y obstaculizar su comunicación enmascarando la transmisión del emisor.

El timbre _Candela 7279_ posee un alcance de hasta 100 metros. Para evaluar el ataque de interferencia, se ubicó el módulo CC1101 junto al receptor del timbre.

Partiendo de una distancia de centímetros entre el emisor y el receptor, se presionaba el botón del control remoto mientras se aumentaba progresivamente la distancia hasta que el timbre dejaba de responder. En el momento en que el receptor dejaba de funcionar, se alejaba el CC1101 del mismo, observando que el timbre volvía a operar con normalidad, lo que indicaba que la señal generada por el módulo dejaba de enmascarar la transmisión del emisor.

La distancia máxima a la cual el ataque resultó efectivo mientras el módulo estaba junto al receptor fue de aproximadamente 6 metros. De esta manera, se confirmó el correcto funcionamiento del ataque de interferencia.

=== Notas adicionales de los ensayos

Durante las pruebas con el timbre se identificaron dos inconvenientes:

1. Al intentar capturar la señal transmitida por el timbre _Candela 7279_, no se detectó ningún patrón reconocible, la señal observada presentaba características similares a ruido.
2. Al ejecutar el ataque de interferencia, fue necesario acercar considerablemente la antena del módulo _CC1101_ al receptor del timbre _Candela 7279_, ya que a mayor distancia la interferencia no producía efecto.

Se considera que estos problemas podrían deberse al uso de una antena con una longitud no adecuada para la frecuencia de operación.

El módulo CC1101 utilizado cuenta con una antena de #cm(17.3), correspondiente a la longitud óptima para una frecuencia de #MHz(433.92). Sin embargo, para una frecuencia de #MHz(315), la longitud de antena recomendada es de aproximadamente #cm(23.8).

Para poner a prueba esta hipótesis, se deberá reemplazar la antena del módulo CC1101 por una de longitud adecuada y repetir los mismos ensayos, con el objetivo de evaluar si se obtienen resultados diferentes.

== Ensayos de integración

Para verificar el funcionamiento integral del dispositivo, se realizaron varios ensayos con distintos dispositivos cotidianos. Principalmente, se buscó poder interceptar y replicar sus señales.

Para IR, se confeccionó una tira LED con un Arduino, cuatro LED y un control remoto, como se aprecia en la @fig-tira-led. El mismo cuenta con varios comandos que realizan distintas acciones.

#figure(
  rotate(-90deg, reflow: true, image("../imagenes/ensayo-integracion-ir.png", height: 10cm)),
  caption: [Emulador de tira de LED con control.],
) <fig-tira-led>

Con Médium se logró replicar correctamente el botón "1". El análisis del mismo puede apreciarse en la @fig-signals-ir, realizado con `signals.py`. Se puede apreciar en la captura los metadatos de NEC, incrustados por Médium sobre el archivo final. Además, `signals.py` grafica "0" y "1" cuando detecta que la señal es NEC para ayudar a visualizar el paquete. Particularmente, se verifica que se envían ocho "0" seguidos de ocho "1" (eso es, envía el _address_ seguido del _address_ negado) y ```c 0x0c``` seguido de ```c 0xf3``` (eso es, envía el _command_ seguido del _command_ negado).

#figure(
  image("../imagenes/ensayo-integracion-ir-signal.png"),
  caption: [Análisis de un botón de un control remoto IR.],
  placement: top,
) <fig-signals-ir>

Luego, se probó a captar el control remoto ZAP (de la @fig-control-zap). El mismo se captó a #MHz(433.92) con una modulación AM650 (OOK con un ancho de banda de #kHz(650)) y se graficó la @fig-signals-rf con `signals.py`. Sobre esta señal no se puede apreciar nada en específico porque no se logró descubrir qué protocolo utiliza. sí se puede apreciar dos tipos de pulsos equiespaciados: uno corto y uno largo. Eso probablemente resulte en "0" y "1" digitales --- seguramente no todos sean la clave en sí, ya que el protocolo probablemente incluya algunos bits de verificación.

#figure(
  image("../imagenes/ensayo-integracion-rf-signal.png"),
  caption: [Análisis de un botón de un control remoto RF.],
  placement: top,
) <fig-signals-rf>

Estas pruebas verifican el correcto funcionamiento de Médium para estos dispositivos, pero no para todos. En el caso de RF, no se logró replicar la señal de un timbre inalámbrico que alega trabajar a #MHz(433) --- se intentó captar en OOK y en FSK sin resultados satisfactorios. Si se quisiera ampliar la gama de dispositivos compatibles, se deben realizar más pruebas.
