#import "../utils/units.typ": *

= Diseño de hardware

Se contará con distintos módulos de entrada/salida que se comunicarán con la EDU-CIAA-NXP, que se encargará de coordinarlos.
== Diseño de Esquemático

=== Teclas (_entrada_)

Para la etapa de entradas de usuario se implementó un conjunto de dispositivos que permiten tanto el control digital como analógico del sistema.

En primer lugar, se utilizó un bloque analógico conformado por dos potenciómetros lineales que permiten registrar desplazamientos en coordenadas cartesianas. Cada potenciómetro se comporta como un divisor resistivo variable, de modo que al girar el eje se modifica la tensión presente en su terminal de salida. Esta variación de tensión se mide mediante ADC (_analog-to-digital converter_) integrados en la EDU-CIAA-NXP, utilizando los canales `CH1` y `CH2`. De esta forma, es posible representar movimientos continuos en dos dimensiones. La finalidad de estos potenciómetros es permitir al usuario navegar por el menú y controlar la visualización de datos en pantalla.

En complemento, el módulo incluye un pulsador digital, lo que brinda una señal de entrada binaria (presionado / no presionado) que puede emplearse para acciones de control adicionales.

Además, se incorporaron dos pulsadores independientes, conectados a los pines digitales `GPIO2` y `GPIO4`. En estos casos, se aplicará una rutina de anti-rebote por software y hardware, con el fin de garantizar la estabilidad de la lectura y evitar falsas detecciones ocasionadas por el rebote mecánico de los contactos.

Finalmente, el sistema dispone de un interruptor principal (_switch_) encargado de habilitar o inhabilitar la alimentación general. Este se conecta en serie entre el paquete de baterías y el regulador de tensión, actuando como interruptor maestro de encendido.

El conexionado del mismo se presenta en la @fig-esquematico-botones.

#figure(
  image("../imagenes/esquematico-botones.png", width: 60%),
  caption: [Esquemático de entradas.],
  placement: top,
) <fig-esquematico-botones>

=== Pantalla OLED (_salida_)

Para la etapa de salida de información se seleccionó un display OLED de 1,3 pulgadas con resolución de 128×64 píxeles. Este tipo de pantalla resulta adecuada para el proyecto, ya que permite visualizar datos numéricos, texto descriptivo y representaciones gráficas simples de las señales capturadas, todo en un formato compacto y de bajo consumo.

El principio de funcionamiento de la pantalla OLED se basa en diodos orgánicos emisores de luz, lo que brinda ventajas frente a pantallas LCD tradicionales: no requiere retroiluminación, posee alto contraste y ofrece un ángulo de visión amplio. La resolución de 128×64 es suficiente para mostrar información estructurada como menús, estados de conexión y tramas de señal.

La comunicación con la EDU-CIAA-NXP se realiza mediante el protocolo I2C, lo cual reduce el número de pines necesarios a solo dos líneas compartidas:
- `I2C_SCL`: línea de reloj generada por la EDU-CIAA-NXP.
- `I2C_SDA`: línea de datos bidireccional para transmitir la información.

El módulo se alimenta directamente con #V[3.3] provistos por la propia placa, lo cual asegura compatibilidad eléctrica sin necesidad de conversores de nivel. Esta conexión de detalla en la @fig-esquematico-oled.

#figure(
  image("../imagenes/esquematico-oled.png"),
  caption: [Esquemático del display OLED.],
) <fig-esquematico-oled>

=== Módulo emisor/receptor infrarrojo (_entrada/salida_)

Para la captura de señales infrarrojas se eligió el módulo TSOP38438 como receptor IR. Este componente incorpora un filtro óptico y un demodulador interno sintonizado a #kHz[38], lo que permite obtener una señal digital ya procesada, más limpia y con mayor inmunidad al ruido eléctrico y a la luz ambiente. El módulo se alimenta con #V[5] y entrega una salida digital activa en bajo cuando detecta una portadora infrarroja.

Para la transmisión se optó por un LED infrarrojo, acompañado por una etapa de amplificación basada en un transistor 2N2222 en configuración de conmutación. Esta etapa permite incrementar la corriente que atraviesa el LED, ajustando así la potencia y el alcance de la señal emitida. De esta manera, es posible calibrar la intensidad del transmisor para asegurar una correcta comunicación con distintos dispositivos. Este detalle se grafica en la @fig-circuito-TSOP.

La interfaz eléctrica con la EDU-CIAA-NXP (diagramada en la @fig-esquematico-ir) se realizó de la siguiente manera:
- Receptor IR (TSOP38438) conectado a la entrada digital `GPIO7`.
- Emisor IR (LED + 2N2222) controlado desde la salida digital `GPIO5`.

// #grid(
//   columns: 2,
//   [
//     #figure(
//       image("assets/circuitoTSOP38438.png"),
//       caption: [Esquemático interno de TSOP38438.],
//     ) <CircuitoTSOP>
//   ],
//   [
//     #figure(
//       image("pcb_layers_front/EsquematicomóduloIR.png"),
//       caption: [Esquemático módulo IR.],
//     ) <esquematicoIR>
//   ]
// )



#grid(
  columns: 2,
  column-gutter: 5em,
  [
    #figure(
      image("../imagenes/TSOP38438.png"),
      caption: [Esquemático interno de TSOP38438.],
    ) <fig-circuito-TSOP>
  ],
  [
    #figure(
      image("../imagenes/esquematico-ir-fixed.png"),
      caption: [Esquemático módulo IR.],
    ) <fig-esquematico-ir>
  ],
)

=== Módulo emisor/receptor de radiofrecuencia (_entrada/salida_)

Para la etapa de radiofrecuencia se utiliza el transceptor CC1101, alimentado a #V[3.3] y conectado por SPI. Dentro del proyecto, el módulo se opera en la banda #MHz[433.92], con captura/replay OOK asíncrono.

En firmware se definieron dos perfiles de modulación soportados para el CC1101:

- `AM650`
- `AM270`

Estos perfiles se seleccionan desde la interfaz al momento de grabar en RF y se guardan como metadata dentro del archivo `.sig`, para que en la reproducción se pueda restablecer automáticamente la misma configuración de modulación.

#figure(
  image("../imagenes/esquematico-rf.png"),
  caption: [Esquemático módulo Radiofrecuencia.],
) <fig-esquematico-rf>

La elección del CC1101 se justifica por:
- La flexibilidad en protocolos: permite captar tanto tramas estándar como propietarias.
- El bajo consumo de energía, adecuado para sistemas embebidos.

El módulo se conecta a la EDU-CIAA-NXP a través de la interfaz SPI0, compartida también con el módulo microSD. En este esquema, cada dispositivo cuenta con un pin de _chip select_ independiente, permitiendo que la EDU-CIAA-NXP seleccione cuál de los periféricos SPI está activo en cada momento. Y ademas este módulo posee dos pines `RF_GDO0` y `RF_GDO2` que se conectan a pines `GPIO8` y `GPIO6` respectivamente, que sirven para recibir interrupciones del CC1101 (ej: "paquete recibido" o "buffer vacío").

El CC1101 trabaja con señales de radiofrecuencia analógicas en bandas ISM/SRD, pero a nivel de microcontrolador se controla digitalmente por SPI y por sus pines GDO. De esta manera, la EDU-CIAA-NXP configura frecuencia/modulación, captura los tiempos de nivel lógico y luego puede reconstruir la señal para replay.

=== Memoria microSD  (_entrada/salida_)

El módulo microSD es utilizado en este proyecto como memoria externa de almacenamiento persistente. Su función principal es guardar las señales captadas (tanto IR como RF) y los patrones decodificados para su posterior análisis, visualización o reproducción.

Se usó un modelo genérico sin nombre coloquialmente conocido como "microSD card module", popularizado por su compatibilidad con Arduino. Cuenta con una interfaz de comunicación SPI y su tensión de alimentación #V[5]. Su consumo típico se encuentra entre #mA(20) y #mA(50) según la operación de lectura/escritura.

La conexión del módulo microSD a la EDU-CIAA-NXP se realiza a través de la interfaz SPI0, compartida también con el módulo CC1101. La comunicación está soportada por la biblioteca _FatFs_ incluida en el `firmware_v3` @firmware_v3, lo que asegura compatibilidad con sistemas de archivos FAT16/FAT32.

#figure(
  image("../imagenes/esquematico-microsd.png"),
  caption: [Esquemático módulo microSD.],
) <fig-esquematico-miocrosd>


==== Modificaciones

Al conectar el módulo de microSD y el CC1101, se detectaron problemas en el bus SPI. Luego de depurar, se concluyó que el problema provenía del módulo de microSD: el mismo no libera la línea MISO correctamente. Según se logró investigar, usuarios de foros de ayuda atribuyen el problema al _level shifter_ incluido.

Este componente se agrega para elevar los #V(3.3) de salida nativos de la microSD a #V(5) para ser compatible con el GPIO de Arduino. Estos módulos ademas cuentan con una tecla para alternar entre las distintas tensiones de salida.

Como el GPIO de la EDU-CIAA-NXP trabaja a #V(3.3), no es necesario utilizar el _level shifter_. Así, se realizó un puente que lo evita, como se puede apreciar en la @fig-microsd-fix.

#figure(
  image("../imagenes/foto-microsd-fix.jpg", height: 5cm),
  caption: [Modificación del módulo de microSD.],
) <fig-microsd-fix>

=== Alimentación

El sistema se alimentará mediante un banco de pilas para mantener la portabilidad del prototipo. El banco provee un voltaje nominal cercano a #V[10], por lo que se adopta la siguiente arquitectura de alimentación:

- Entrada del pack de pilas #sym.arrow.r Bornera de entrada.
- Regulador DC-DC #V[10] #sym.arrow #V[5] (principal). El regulador entrega la alimentación a la EDU-CIAA-NXP y a los módulos que aceptan #V[5].
- Regulador adicional #V[5] → #V[3.3]. Este regulador se incorpora para alimentar de forma dedicada los periféricos que operan a #V[3.3]. De esta manera se evita sobrecargar la salida de #V[3.3] de la EDU-CIAA-NXP, garantizando estabilidad en la alimentación y protegiendo tanto a la placa como a los dispositivos externos.

En la @tabla-consumo se detallan consumos estimados por bloque, el cálculo total y las decisiones de diseño.

#figure(
  table(
    columns: 3,
    stroke: none,
    align: (left, center, left),
    table.hline(),
    table.header[*Componente*][*Corriente típica*\ [#mA()]][*Notas*],
    table.hline(),
    [EDU-CIAA-NXP], [150], [Consumo típico en operación.],
    [CC1101], [30], [Durante transmisión.],
    [microSD], [100], [Picos hasta #mA(100) en escritura.],
    [OLED 1.3" 128×64], [30], [Brillo medio, picos mayores si contraste al máximo.],
    [TSOP38438], [5], [Módulo receptor (pequeño).],
    [IR LED], [100], [Corriente pico cuando transmite.],
    [Potenciómetros y pulsadores], [5], [Consumo pasivo pequeño.],
    [Otros], [10], [Pérdidas menores.],
    table.hline(),
    [*Total nominal*], [*440*], [],
    table.hline(),
  ),
  caption: [Consumo de los componentes.\ _Otros_ refiere a resistencias divisoras nivel, anti rebote, y demás componentes pasivos.],
) <tabla-consumo>

=== Resumen

El esquemático completo puede consultarse en el @ap-esquematico. Además, en el @ap-materiales se presenta la lista completa de insumos del proyecto.


== Diseño del circuito impreso <sc-diseño-pcb>

El circuito impreso desarrollado presenta las características técnicas de la @tabla-caracteristicas-pcb. El diseño fue pensado para acoplarse mecánica y eléctricamente a la placa EDU-CIAA-NXP, utilizando conectores dobles de 2x20 pines para la interconexión.

#figure(
  table(
    columns: 2,
    align: (left, right),
    table.header[*Propiedad*][*Valores*],
    [Ancho], mm(120),
    [Alto], mm(85),
    [Cantidad de capas], [1],
    [Tipo de capas], [simple faz],
    [Ancho de pista], mm(1),
    [#sym.diameter interno de los pads], mm(0.7),
    [#sym.diameter externo de los pads], mm(1.7),
  ),
  caption: [Características del PCB.],
) <tabla-caracteristicas-pcb>


=== Proceso de diseño del PCB

El proceso de diseño comenzó con la definición de las restricciones físicas, estableciendo las dimensiones totales de la placa y la ubicación de los conectores de interconexión con la EDU-CIAA. Una vez fijadas estas condiciones, se procedió al posicionamiento de los componentes, agrupando los circuitos según su función y procurando que aquellos que interactúan entre sí se mantuvieran próximos en el PCB. Este criterio redujo la longitud de las pistas y facilitó el enrutado, además de disminuir el riesgo de interferencia entre señales.

Posteriormente, se inició el ruteo de las conexiones, comenzando por las pistas más simples y de menor recorrido, para luego abordar las conexiones más extensas o críticas. En esta etapa se realizaron ajustes en la asignación de pines hacia la EDU-CIAA, buscando minimizar la cantidad de cruces y puentes necesarios. Se dio prioridad a las líneas de señal frente a las de alimentación, a fin de garantizar una correcta transmisión de datos y una disposición estable del sistema.

Debido a la limitación de trabajar en una sola cara, fue necesario incorporar algunos puentes de conexión, principalmente en el bus SPI, en la alimentación de #V(3.3) del módulo RF y para unir dos islas de GND que quedaban aisladas. Estas soluciones permitieron mantener la integridad del diseño sin comprometer la funcionalidad ni la estética del circuito.

El resultado final es una placa compacta, ordenada y funcional, que integra de manera eficiente todos los módulos del sistema: el módulo RF, el receptor y emisor IR, el módulo microSD, la pantalla OLED, los potenciómetros de entrada, los pulsadores, y el sistema de regulación de tensión. En el @ap-pcb se detallan los diseños PCM.

#figure(
  image("../imagenes/foto-completo.jpeg"),
  caption: [Fotografía de _Médium_ con todos sus componentes.],
) <fig-foto-completo>
