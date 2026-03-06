// Informe final
// Escrito con Typst 0.14.2
// typst compile --pdf-standard a-2u main.typ informe.pdf

#import "@preview/barcala:0.3.0": apendice, informe
#import "@preview/lilaq:0.4.0" as lq
#import "@preview/zero:0.5.0"

#show: informe.with(
  unidad-academica: "ingeniería",
  asignatura: "E0306 Taller de Proyecto I",
  trabajo: "Informe final",
  equipo: "Grupo 5",
  autores: (
    (
      nombre: "Majoros, Lorenzo",
      email: "lorenzomajoros@alu.ing.unlp.edu.ar",
      legajo: "03296/1",
    ),
    (
      nombre: "Nihany, Santiago",
      email: "santiago.nihany@alu.ing.unlp.edu.ar",
      legajo: "03012/3",
    ),
    (
      nombre: "Triviño Loyola, Gastón Eduardo",
      email: "gastontrivino@alu.ing.unlp.edu.ar",
      legajo: "02997/2",
    ),
    (
      nombre: "Seery, Juan Martín",
      email: "juan.seery@alu.ing.unlp.edu.ar",
      legajo: "03471/9",
      notas: "Autor responsable del informe",
    ),
  ),

  titulo: [_Médium_: dispositivo para captar y transmitir señales],

  fecha: "2026-02-09",
)

// Enlaces de colores
#show cite: set text(blue)
#show link: set text(blue)
#show ref: set text(blue)

// Bloques de matemática con números para citar
#set math.equation(numbering: "(1)")
#show ref: it => {
  if it.element != none and it.element.func() == math.equation {
    // Sobreescribir las referencias a ecuaciones
    link(it.element.location(), numbering(
      it.element.numbering,
      ..counter(math.equation).at(it.element.location()),
    ))
  } else {
    // Otras referencias quedan igual
    it
  }
}

// Configuración de `zero`
#import zero: num, zi
#zero.set-num(
  decimal-separator: ",",
)
#zero.set-group(
  size: 3,
  separator: ".",
  threshold: (integer: 5, fractional: calc.inf),
)
#zero.set-unit(
  fraction: "inline",
)

// Contenidos
#outline()
#pagebreak()
#include "secciones/01-introduccion.typ"
#include "secciones/02-objetivos.typ"
#pagebreak()
#include "secciones/03-requerimientos.typ"
#include "secciones/04-hardware.typ"
#pagebreak()
#include "secciones/05-firmware.typ"
#pagebreak()
#include "secciones/06-ensayos.typ"
#pagebreak()
#include "secciones/07-conclusiones.typ"
#bibliography("bibliografia.bib")

#pagebreak()
#show: apendice
#set heading(supplement: [Apéndice])
= Apéndices
#include "secciones/A1-materiales.typ"
#pagebreak(weak: true)
#include "secciones/A2-presupuesto.typ"
#pagebreak(weak: true)
#include "secciones/A3-esquematico.typ"
#pagebreak(weak: true)
#include "secciones/A4-PCB.typ"
#pagebreak(weak: true)
#include "secciones/A5-manual.typ"
#pagebreak(weak: true)
#include "secciones/A6-archivos.typ"
