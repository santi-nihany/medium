// FUNCIONES PARA GRAFICAR RF

#let hex-to-int(hex-str) = {
  let digits = "0123456789ABCDEF"
  let result = 0
  let hex-upper = upper(hex-str)
  
  for char in hex-upper.codepoints() {
    result = result * 16
    let digit-val = digits.position(char)
    if digit-val != none {
      result = result + digit-val
    }
  }
  
  result
}

#let draw-RF-signal(
  hex-string, 
  name: "Señal digital #1",
  bit-time: 50,              // tiempo en microsegundos entre bits
  bit-width: 8pt,            // ancho visual de cada bit
  bytes-per-line: none,      // número de bytes antes de crear un nuevo gráfico
  time-mark-interval: 2000,  // intervalo en microsegundos para marcas de tiempo
  options: (:)
) = {
  // Configuración por defecto
  let config = (
    high-color: rgb("#4a90e2"),
    low-color: white,
    stroke-width: 1.5pt,
    stroke-color: rgb("#2e5c8a"),
    ..options
  )
  
  // Parsear la cadena hexadecimal (sin espacios)
  let hex-clean = hex-string.replace(" ", "")
  let hex-values = ()
  
  // Dividir en bytes (cada 2 caracteres)
  let i = 0
  while i < hex-clean.len() {
    let byte = hex-clean.slice(i, calc.min(i + 2, hex-clean.len()))
    if byte.len() == 2 {
      hex-values.push(byte)
    } else if byte.len() == 1 {
      // Si queda un solo carácter, agregarlo con padding
      hex-values.push(byte + "0")
    }
    i += 2
  }
  
  // Dividir en líneas según bytes-per-line
  let lines = ()
  if bytes-per-line == none {
    lines.push(hex-values)
  } else {
    let current-line = ()
    for (idx, byte) in hex-values.enumerate() {
      current-line.push(byte)
      if calc.rem(idx + 1, bytes-per-line) == 0 or idx == hex-values.len() - 1 {
        lines.push(current-line)
        current-line = ()
      }
    }
  }
  
  // Función auxiliar para dibujar una línea de señal
  let draw-signal-line(hex-bytes, line-number, total-lines) = {
    let bits = ()
    
    for hex-val in hex-bytes {
      let decimal = hex-to-int(hex-val)
      // Convertir a 8 bits
      for i in range(8) {
        let bit = calc.rem(calc.quo(decimal, calc.pow(2, 7 - i)), 2)
        bits.push(if bit == 1 { 1 } else { 0 })
      }
    }
    
    let total-bits = bits.len()
    let signal-height = 40pt
    let total-width = total-bits * bit-width
    
    // Crear el gráfico
    block(
      width: 100%,
      stroke: none,
      [
        #set text(size: 9pt)
        // #if line-number == 1 {
        //   let total-samples = hex-values.len() * 8
        //   [
        //     #align(center)[
        //       #text(weight: "bold", size: 11pt)[
        //         #name (#total-samples muestras)
        //       ]
        //     ]
        //     #v(0.5em)
        //   ]
        // }
        
        // Contenedor del gráfico
        #box(
          width: 100%,
          {
            // Etiquetas del eje Y
            grid(
              columns: (auto, 1fr),
              column-gutter: 0.3em,
              row-gutter: 0em,
              
              // HIGH label
              align(center + top)[HIGH],
              box(height: signal-height / 2-5pt),
              
              // LOW label  
              align(center + bottom)[
                #v(1em)
                LOW],
              box(height: signal-height / 2-2pt),
            )
            
            v(-signal-height - 0.5em)
            
            // La señal
            box(
              width: 100%,
              height: signal-height,
              {
                place(
                  dx: 3em,
                  stack(
                    dir: ltr,
                    spacing: 0pt,
                    ..bits.enumerate().map(((i, bit)) => {
                      // Determinar qué bordes dibujar
                      let prev-bit = if i > 0 { bits.at(i - 1) } else { none }
                      
                      // Solo dibujar borde izquierdo si el bit anterior es diferente
                      let draw-left = (prev-bit == none) or (prev-bit != bit)
                      
                      box(
                        width: bit-width,
                        height: signal-height,
                        fill: if bit == 1 { config.high-color.lighten(70%) } else { white },
                        stroke: (
                          top: if bit == 1 { config.stroke-width + config.stroke-color } else { none },
                          bottom: if bit == 0 { config.stroke-width + config.stroke-color } else { none },
                          left: if draw-left { config.stroke-width + config.stroke-color } else { none },
                          right: none,
                        ),
                      )
                    })
                  )
                )
                
                // Borde derecho final
                place(
                  dx: 3em + total-width,
                  box(
                    width: 0pt,
                    height: signal-height,
                    stroke: (left: config.stroke-width + config.stroke-color)
                  )
                )
              }
            )
            
            v(0.5em)
            
            // Eje X con marcas de tiempo
            box(
              width: 100%,
              {
                place(
                  dx: 3em,
                  {
                    // Calcular cuántos bits representa cada marca de tiempo
                    let bit-interval = calc.quo(time-mark-interval, bit-time)
                    
                    // Calcular el offset de tiempo para esta línea
                    let time-offset = 0
                    if bytes-per-line != none and line-number > 1 {
                      time-offset = (line-number - 1) * bytes-per-line * 8 * bit-time
                    }
                    
                    stack(
                      dir: ltr,
                      spacing: 0pt,
                      ..range(0, total-bits + 1, step: bit-interval).map(i => {
                        if i * bit-width <= total-width {
                          let time-val = time-offset + (i * bit-time)
                          box(
                            width: bit-interval * bit-width,
                            align(left)[
                              #line(length: 5pt, angle: 90deg, stroke: gray)
                              #v(-0.3em)
                              #text(size: 7pt)[#time-val]
                            ]
                          )
                        }
                      })
                    )
                  }
                )
                
                v(1.5em)
                
                if line-number == total-lines [
                   #text(" ",size:15pt)
                 #align(center)[
                   #text(size: 10pt)[Tiempo (μs)]
                 ]
               ]
              }
            )
          }
        )
      ]
    )
  }
  
  // Dibujar todas las líneas
  for (idx, line) in lines.enumerate() {
    draw-signal-line(line, idx + 1, lines.len())
    if idx < lines.len() - 1 {
      v(0.5em)
    }
  }
}