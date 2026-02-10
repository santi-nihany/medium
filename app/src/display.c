//===----------------------------------------------------------------------===//
///
/// \file
/// Librería para utilizar un display OLED.
///
//===----------------------------------------------------------------------===//

#include "display.h"
#include "sh1106.h"

#define _swap_bytes(a, b)                                                      \
  {                                                                            \
    uint8_t tmp = a;                                                           \
    a = b;                                                                     \
    b = tmp;                                                                   \
  }

/// Inicialización del display
void displayInit() { sh1106_init(); }

/// Cambia el color de un píxel
/// \param x columna del píxel, comenzando desde 0
/// \param y fila del píxel, comenzando desde 0
void displayDrawPixel(uint8_t x, uint8_t y, DisplayColor color) {
  sh1106_draw(x, y, color);
}

/// Dibuja una línea
/// \param x0 columna de inicio, comenzando desde 0
/// \param y0 fila de inicio, comenzando desde 0
/// \param x1 columna final, comenzando desde 0
/// \param y1 fila final, comenzando desde 0
/// \param color color de la línea
void displayDrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,
                     DisplayColor color) {
  bool_t steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep) {
    _swap_bytes(x0, y0);
    _swap_bytes(x1, y1);
  }

  if (x0 > x1) {
    _swap_bytes(x0, x1);
    _swap_bytes(y0, y1);
  }

  uint8_t dx, dy;
  dx = x1 - x0;
  dy = abs(y1 - y0);

  uint8_t err = dx / 2;
  uint8_t ystep;

  if (y0 < y1) {
    ystep = 1;
  } else {
    ystep = -1;
  }

  for (; x0 <= x1; x0++) {
    if (steep) {
      displayDrawPixel(y0, x0, color);
    } else {
      displayDrawPixel(x0, y0, color);
    }
    err -= dy;
    if (err < 0) {
      y0 += ystep;
      err += dx;
    }
  }
}

/// Dibuja un rectángulo
/// @param x columna de inicio, comenzando desde 0
/// @param y fila de inicio, comenzando desde 0
/// @param width ancho del rectángulo
/// @param height alto del rectángulo
/// @param color color del borde/relleno
/// @param filled si el rectángulo está relleno
void displayDrawRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height,
                          DisplayColor color, bool_t filled) {
  if (filled) {
    for (uint8_t i = 0; i < width; i++) {
      displayDrawLine(x + i, y, x + i, y + height - 1, color);
    }
  } else {
    width--;
    height--;
    displayDrawLine(x, y, x + width, y, color);
    displayDrawLine(x, y, x, y + height, color);
    displayDrawLine(x + width, y + height, x, y + height, color);
    displayDrawLine(x + width, y + height, x + width, y, color);
  }
}

/// Copia una imagen en el display
/// Se espera que la imagen esté almacenada en forma de columnas little-endian,
/// donde 0 es fondo negro y 1 es blanco.
/// \param sprite la imagen a colocar
/// \param x columna de inicio, comenzando desde 0
/// \param y fila de inicio, comenzando desde 0
/// \note
/// Digamos que se quiere almacenar un "1" de 5x10. El mismo se verá así en
/// memoria:
/// ```c
/// const uint8_t image[] = {
///   0b00001000,
///   0b00000100,
///   0b11111110,
///   0b00000000,
///   0b00000000,
///   // ---
///   0b00000001,
///   0b00000001,
///   0b00000001,
///   0b00000001,
///   0b00000001
/// };
/// ```
/// Puede apreciarse que primero se almcenan todas las columnas de la parte
/// superior del "1" y luego las columnas inferiores. Si el tamaño de la imagen
/// no es un un múltiplo de 8, las últimas columnas tendrán ceros en su parte
/// más significativa. Se recomienda utilizar la página
/// https://notisrac.github.io/FileToCArray/ para generar imágenes en este
/// formato, con
/// ```txt
/// Palette mod = 1bit
/// Optimize for column read = sí
/// Separate bytes of pixels = sí
/// Endianness = Little-endian
/// Data type = uint8_t
/// ```
void displayPlace(const Sprite sprite, uint8_t x, uint8_t y) {
  sh1106_place(sprite, x, y);
}

/// Actualiza el display con lo escrito en el buffer
void displayUpdate() { sh1106_update(); }