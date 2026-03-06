#import "../utils/units.typ": *


= Conclusiones

A lo largo del semestre, se logró implementar un dispositivo que permite trabajar con señales de infrarrojos y radiofrecuencias. Se cumplieron todos los objetivos primarios: detectar y muestrear señales IR/RF, perdurarlas en una memoria microSD y la capacidad de realizarse desde el mismo dispositivo (es decir, la implementación de una interfaz física para el usuario --- sin necesidad de un dispositivo externo para su funcionamiento).

Los objetivos secundarios no fueron realizados. Uno de ellos consta de interpretar protocolos comunes desde el mismo dispositivo — el mismo no pudo ser cumplido por falta de tiempo y pobre variedad de aparatos emisores para ensayar. Otro objetivo constaba de visualizar la forma de las señales con la pantalla de _Médium_ --- resultó fatuo para los gráficos más complejos debido al tamaño de pantalla limitante.

La realización del proyecto no ha presentado muchos inconvenientes. El principal de ellos fue la poca variedad de protocolos de RF para probar el módulo. Otro detalle que no se consideró en el diseño inicial es la ergonomía del dispositivo: podría fabricarse una caja de plástico (por ejemplo) donde ubicar el dispositivo de tal manera que el usuario pueda sujetarlo con ambas manos.

== Cumplimiento

A continuación, se detallan los requerimientos planteados junto a
- #emoji.circle.green para indicar su cumplimiento satisfactorio,
- #emoji.circle.yellow para indicar su cumplimiento parcial o
- #emoji.circle.red para indicar que no pudo ser cumplido.

De ser necesario, se adjunta alguna aclaración adicional.

- *Requerimientos de los periféricos (funcionales)*
  - El sistema debe contar con un transductor de señales IR #emoji.circle.green.
  - El sistema debe ser capaz de demodular señales IR digitales #emoji.circle.green.
  - El sistema debe ser capaz de transmitir señales IR #emoji.circle.green.
  - El sistema debe contar con un transductor de señales de RF #emoji.circle.green.
  - El módulo RF debe poder captar señales en bandas ISM/SRD comunes #emoji.circle.green (#MHz(433)).
  - El módulo RF debe poder demodular señales digitales comunes (mínimamente OOK) #emoji.circle.green (solo OOK).
  - El sistema debe ser capaz de transmitir señales RF  #emoji.circle.green.
  - El dispositivo debe contar con un puerto para conectar algún dispositivo de almacenamiento masivo como microSD  #emoji.circle.green (puerto para microSD).
  - El dispositivo debe contar con una pantalla para mostrar la interfaz de usuario #emoji.circle.green (pantalla OLED).
  - El dispositivo debe contar botones (o similares) para que el usuario interactúe con el mismo #emoji.circle.green (joystick).
- *Requerimientos del sistema (funcionales)*
  - El sistema debe contar con una interfaz de usuario para seleccionar distintos modos de funcionamiento #emoji.circle.green (permite caputar/emitir IR/RF).
  - La interfaz de usuario debe mostrarse en la pantalla #emoji.circle.green.
  - La interfaz de usuario debe responder a los botones presionados por el usuario #emoji.circle.green.
  - Cada periférico debe poder ser activado cuando el usuario lo necesite #emoji.circle.green.
  - El dispositivo debe permitir almacenar las señales captadas en el sistema de almacenamiento #emoji.circle.green.
  - El dispositivo debe reproducir señales almacenadas a través de los periféricos #emoji.circle.green.
  - El sistema debe permitir eliminar señales almacenadas del sistema de almacenamiento #emoji.circle.green.
  - El sistema debe registrar la fecha y hora de las capturas realizadas para su posterior trazabilidad #emoji.circle.red.
    - Este requerimiento se descartó porque se hubiera necesitado de un módulo extra que maneje fecha y hora. Se reemplazó por guardar las captura en uno de varios _slots_ prefijados.
- *Requerimientos del proyecto (no funcionales)*
  - El proyecto debe desarrollarse utilizando el microcontrolador EDU-CIAA-NXP @CIAA como plataforma principal #emoji.circle.green (única plataforma).
  - El código debe estar desarrollado en C/C++, compatible con los entornos de trabajo de la EDU-CIAA-NXP #emoji.circle.green.
  - Todos los elementos físicos deben estar ensamblados en un único dispositivo final #emoji.circle.green.
  - El diseño del dispositivo debe ser compacto y seguro para su manipulación #emoji.circle.yellow.
    - El diseño del PCB fue pensado para ser lo más compacto posible al momento de encapsularse, pero no se llegó a diseñar una caja que contenga los circuitos.
  - La interfaz diseñada para la pantalla debe ser clara y sencilla, priorizando la facilidad de uso #emoji.circle.green.
  - El transmisión y recepción de señales debe acatarse a las normativas del ENACOM vigentes a la fecha #emoji.circle.green.
  - El almacenamiento de señales debe realizarse según estándares preexistentes #emoji.circle.yellow.
    - Se descubrió que no existen estándares preexistentes almacenar señales digitales (ver @ap-formato-sig).
  - La documentación del software debe incluir diagramas, descripciones de módulos y guías de uso para el usuario final #emoji.circle.green (ver @ap-manual).
  - El proyecto debe estar finalizado, junto con su documentación correspondiente, antes del final de la cursada de "Taller de Proyecto I" del segundo semestre de 2025 #emoji.circle.green.
  - El desarrollo del proyecto debe ajustarse a un presupuesto preestablecido #emoji.circle.green.

== División de tareas

Siguiendo con el tiempo dedicado al proyecto, no se ha desviado tanto del planteo original de la @table-tareas.

#figure(
  table(
    columns: (auto, auto),
    align: (center + horizon, left),
    inset: (x: 0.5em, y: 0.8em),
    table.header[*Integrante*][*Tareas*],

    table.cell(rowspan: 2)[Juan \ Santiago],
    [Integración del módulo microSD y desarrollo del sistema de archivos.],
    [Implementación de la interfaz de usuario: joystick y pantalla.],

    table.cell(rowspan: 2)[Lorenzo],
    [Integración del módulo de Radiofrecuencia.],
    [Desarrollo de rutinas de captura y almacenamiento de tramas RF.],

    table.cell(rowspan: 2)[Gastón],
    [Integración del módulo Infrarrojo (receptor y emisor).],
    [Desarrollo de rutinas de captura, análisis y reproducción de señales IR.],

    table.cell(rowspan: 3)[_Todos_],
    [Implementación conjunta de aplicaciones IR y RF.],
    [Diseño de la PCB.],
    [Ensamblaje final del dispositivo y validación de funcionamiento],
  ),
  caption: [División orignal de tareas],
) <table-tareas>

Juan terminó encargándose del driver de la pantalla OLED, del joystick y  del diseño de las distintas pantallas, mientras que Santiago que dedicó al módulo microSD. Lorenzo y Gastón se ocuparon de los módulos RF e IR respectivamente.

El diseño del PCB fue realizado entre todos, pero con mayor impronta de Lorenzo y Gastón que ya contaban con experiencia previa de su paso por una secundaria técnica.

No se llevó un registro riguroso, pero se estiman 4 horas semanales aportadas al proyecto por cada integrante del grupo, considerando armado del PCB, realización de los informes, investigaciones y pruebas. A lo largo de 18 semanas, se tiene que cada integrante aportó 72 horas al proyecto, resultando en un total de 288 horas de ingeniería.

#figure(
  image("../imagenes/diagrama-gantt-final.png"),
  caption: [Cronograma final realizado.],
)

== Conclusiones generales

Más allá del resultado final satisfactorio, se consideró muy valiosa la experiencia de trabajar en un proyecto un poco más ambicioso para lo que se exige en la carrera. Se aprendió a realizar estimaciones realistas, se claro con los objetivos y la forma de expresarse con los avances. Agradecemos a Juan Ignacio por guiarnos en este aspecto.

Por ello, consideramos que fue un proyecto más que enriquecedor para nuestra formación como futuros ingenieros.
