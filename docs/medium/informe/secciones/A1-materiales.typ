#import "../utils/units.typ": *

== Lista de materiales <ap-materiales>

En la @tabla-insumos se presenta la lista completa de insumos del proyecto, incluyendo los módulos y placas principales, componentes para regulación y alimentación, conectores, elementos de la botonera, y componentes específicos para la sección de IR. Esta lista detalla tanto la cantidad como las notas relevantes para cada componente, facilitando la adquisición y organización de los materiales necesarios para el desarrollo del proyecto.

#figure(
  table(
    columns: 3,
    stroke: none,
    align: (center, left, left),
    table.hline(),
    table.header[*Cantidad*][*Componente*][*Notas*],
    table.hline(),
    table.cell(colspan: 3, align: left)[*Módulos y placas principales*],
    [1], [EDU-CIAA-NXP], [Placa base provista por la cátedra.],
    [1], [Módulo CC1101], [Con interfaz SPI],
    [1], [Módulo microSD], [Con interfaz SPI],
    [1], [OLED 1.3" 128×64], [Monocromático con interfaz I#super[2]C],
    [1], [Módulo IR TSOP38438], [o equivalente TSOP382/384],
    [1], [LED IR emisor], [Debe emitir a #nm[940]],
    table.cell(colspan: 3, align: left)[*Regulación y alimentación*],
    [1], [Banco de pilas], [pack/portapilas],
    [2], [Bornera 2 pines], [Para entrada de pack de pilas],
    [2], [Condensador electrolítico], [De #uF[200]],
    [1], [Regulador 5V], [LM7805],
    [1], [Diodo zener], [1N4728],
    [1], [Capacitor], [De #uF[0.01]],
    [1], [Resistencia], [De #ohm[10]],
    [2], [Diodo rectificador], [1N4007],
    [1], [Capacitor], [De #uF[0.01]],
    table.cell(colspan: 3, align: left)[*Conectores*],
    [1], [Conector hembra], [Jack 2.1mm],
    [2], [Tiras de pines macho], [20×2 (40 pines)],
    [1], [Tiras de pines hembra], [20×1 (20 pines)],
    table.cell(colspan: 3, align: left)[*Botonera*],
    [1], [_Joystick_], [O semejante ensamble de dos potenciómetros.],
    [2], [Pulsadores], [O semejante ensamble de dos potenciómetros.],
    [2], [Condensador], [De #nF[100], para antirrebote],
    [2], [Resistencia], [De #kohm[10]],
    table.cell(colspan: 3, align: left)[*Para IR*],
    [1], [Condensador], [De #uF[2.2], para desacople $V_"CC"$ del receptor.],
    [1], [Transistor], [2N2222],
    [2], [Resistencias], [de polarización],
    table.hline(),
  ),
  caption: [Insumos y componentes de Médium (_bill of materials_).],
) <tabla-insumos>
