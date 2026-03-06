= Análisis de requerimientos

A grandes rasgos, se desarrollará un sistema cuya componente principal sea un microcontrolador que se encargue de gestionar distintos módulos periféricos. Mismamente, los transductores que se permitan captar las señales IR y RF. Además, se contará con algún _display_ y botonera para permitir al usuario seleccionar distintos modos de funcionamiento. Finalmente, alguna conexión con algún método de almacenamiento masivo como microSD. En la @figa-diagrama-bloques se detalla este sistema como diagrama de bloques.

#figure(
  image("../imagenes/diagrama-bloques-componentes.png"),
  caption: [Diagrama de bloques de Médium.],
) <figa-diagrama-bloques>

A continuación, se detallan explícitamente los requerimientos del proyecto. A grandes rasgos, se dividieron según el ámbito al que corresponden: comunicación con el exterior (_periféricos_), control de los periféricos (_sistema_), y otros requerimientos sobre el proyecto (_proyecto_).

== Requerimientos de los periféricos (funcionales)
- El sistema debe contar con un transductor de señales IR.
- El sistema debe ser capaz de demodular señales IR digitales.
- El sistema debe ser capaz de transmitir señales IR.
- El sistema debe contar con un transductor de señales de RF.
- El módulo RF debe poder captar señales en bandas ISM/SRD comunes.
- El módulo RF debe poder demodular señales digitales comunes (mínimamente OOK).
- El sistema debe ser capaz de transmitir señales RF.
- El dispositivo debe contar con un puerto para conectar algún dispositivo de almacenamiento masivo como microSD.
- El dispositivo debe contar con una pantalla para mostrar la interfaz de usuario.
- El dispositivo debe contar botones (o similares) para que el usuario interactúe con el mismo.

== Requerimientos del sistema (funcionales)
- El sistema debe contar con una interfaz de usuario para seleccionar distintos modos de funcionamiento.
- La interfaz de usuario debe mostrarse en la pantalla.
- La interfaz de usuario debe responder a los botones presionados por el usuario.
- Cada periférico debe poder ser activado cuando el usuario lo necesite.
- El dispositivo debe permitir almacenar las señales captadas en el sistema de almacenamiento
- El dispositivo debe reproducir señales almacenadas a través de los periféricos.
- El sistema debe permitir eliminar señales almacenadas del sistema de almacenamiento.
- El sistema debe registrar la fecha y hora de las capturas realizadas para su posterior trazabilidad.

== Requerimientos del proyecto (no funcionales)
- El proyecto debe desarrollarse utilizando el microcontrolador EDU-CIAA-NXP @CIAA como plataforma principal.
- El código debe estar desarrollado en C/C++, compatible con los entornos de trabajo de la EDU-CIAA-NXP
- Todos los elementos físicos deben estar ensamblados en un único dispositivo final.
- El diseño del dispositivo debe ser compacto y seguro para su manipulación.
- La interfaz diseñada para la pantalla debe ser clara y sencilla, priorizando la facilidad de uso.
- El transmisión y recepción de señales debe acatarse a las normativas del ENACOM vigentes a la fecha.
- El almacenamiento de señales debe realizarse según estándares preexistentes.
- La documentación del software debe incluir diagramas, descripciones de módulos y guías de uso para el usuario final.
- El proyecto debe estar finalizado, junto con su documentación correspondiente, antes del final de la cursada de "Taller de Proyecto I" del segundo semestre de 2025.
- El desarrollo del proyecto debe ajustarse a un presupuesto preestablecido.
