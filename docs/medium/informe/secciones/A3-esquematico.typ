#import "../utils/units.typ": *

== Circuitos y esquemáticos <ap-esquematico>

En este apéndice se presentan los esquemáticos y circuitos desarrollados para el proyecto. Por un lado, en la @fig-esquematico-completo se muestra el esquemático completo del poncho realizado para integrar todos los componentes del proyecto. Además, en la @tabla-footprints se detallan los componentes utilizados, sus valores o tipos, y los footprints correspondientes para su montaje.

#figure(
  table(
    columns: 3,
    align: left + horizon,
    table.header[*Ref.*][*Valor/tipo*][*_Footprint_*],

    [C1], [#uF(2.2) — electrolítico radial], `Capacitor_THT: CP_Radial_D6.3mm_P2.50mm`,
    [C2],
    table.cell(rowspan: 2)[#nF(100) — cerámico disco],
    table.cell(rowspan: 2)[`Capacitor_THT: C_Disc_D4.7mm_ W2.5mm_P5.00mm`],
    [C3],
    [C4],
    table.cell(rowspan: 2)[#uF(10) — electrolítico radial],
    table.cell(rowspan: 2)[`Capacitor_THT: CP_Radial_D6.3mm_P2.50mm`],
    [C5],
    [C6], [#nF(100) — cerámico disco], `Capacitor_THT: C_Disc_D4.7mm_ W2.5mm_P5.00mm`,
    [C7], [#nF(100) - cerámico disco], `Capacitor_THT: C_Disc_D3.4mm_ W2.1mm_P2.50mm`,
    [D1], [SFH4550 — LED infrarrojo], `LED_THT: LED_D5.0mm_IRGrey`,
    [D2], [1N4007 - diodo rectificador], `Diode_THT: D_DO-41_SOD81_ P10.16mm_Horizontal`,
    [D3], [`D_Zener` — diodo zener], `Diode_THT: D_DO-34_SOD68_ P10.16mm_Horizontal`,
    [D4], [1N4007 - diodo rectificador], `Diode_THT: D_DO-41_SOD81_ P10.16mm_Horizontal`,
    [JP1],
    table.cell(rowspan: 2)[Conectores EDU-CIAA — header doble 2x20],
    table.cell(rowspan: 2)[`Connector_PinHeader_2.54mm: PinHeader_2x20_P2.54mm_Vertical`],
    [JP2],
    [J3], [RF — conector RF], `Connector_PinSocket_2.54mm: PinSocket_2x04_P2.54mm_Vertical`,
    [J4], [`Micro_SD` - header 1x6], `Connector_PinSocket_2.54mm: PinSocket_1x06_P2.54mm_Vertical`,
    `J5`, [`OLED_128x64` - header 1x4], `Connector_PinSocket_2.54mm: PinSocket_1x04_P2.54mm_Vertical`,
    [J6],
    [`POT_X` — header 1x3],
    table.cell(rowspan: 2)[`Connector_PinSocket_2.54mm: PinSocket_1x03_P2.54mm_Vertical`],
    [J7], [`POT_Y` — header 1x3],
    [J10],
    table.cell(rowspan: 2)[`Power_In` - entrada de alimentación],
    `Connector_PinHeader_2.54mm: PinHeader_1x02_P2.54mm_Vertical`,
    [J11], [`Power_5V` - alimentación #V(5)],
    [Q1], [2N2222 - transistor NPN], `Package_TO_SOT_THT: TO-92L_Inline`,
    [R1],
    [#ohm(33) — #W[1/4]],
    table.cell(rowspan: 6)[`Resistor_THT:R_Axial_DIN0207_L6. 3mm_D2.5mm_P10.16mm_Horizontal`],
    [R2], [#kohm(1) — #W[1/4]],
    [R3], table.cell(rowspan: 2)[#kohm(100) — #W[1/4]],
    [R4],
    [R5], [#kohm(1) — #W[1/4]],
    [R13], [#kohm(100) — #W[1/4]],
    [SW1], table.cell(rowspan: 3)[Pulsador], table.cell(rowspan: 3)[`Button_Switch_THT:SW_PUSH_6mm`],
    [SW2],
    [SW3],
    [SW7], [Switch deslizable], `Button_Switch_THT: SW_Slide_SPDT_Straight_ CK_OS102011MS2Q`,
    [U1], [TSOP38338 — receptor IR], `OptoDevice:Vishay_MOLD-3Pin`,
    [U2], [LM7805 — regulador lineal], `Package_TO_SOT_THT: TO-220-3_Vertical`,
  ),
  caption: [Referencias de los componentes del poncho de Médium.],
) <tabla-footprints>

#figure(
  rotate(-90deg, reflow: true, image("../imagenes/esquematico-completo.png")),
  caption: [Esquemático completo del poncho de Médium.],
) <fig-esquematico-completo>
