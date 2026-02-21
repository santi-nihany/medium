//===----------------------------------------------------------------------===//
///
/// \file
/// Librería para utilizar un display OLED.
///
//===----------------------------------------------------------------------===//

#include "modules/display.h"
#include "drivers/sh1106.h"

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

/// Dibuja un rectángulo con las esquinas redondeadas
/// @param x columna de inicio, comenzando desde 0
/// @param y fila de inicio, comenzando desde 0
/// @param width ancho del rectángulo
/// @param height alto del rectángulo
/// @param color color del borde/relleno
/// @param filled si el rectángulo está relleno
void displayDrawRoundedRectangle(uint8_t x, uint8_t y, uint8_t width,
                                 uint8_t height, DisplayColor color,
                                 bool_t filled) {
  if (width == 0 || height == 0) {
    return;
  }

  // El recorte fijo de esquina requiere al menos 4x4.
  if (width < 4 || height < 4) {
    displayDrawRectangle(x, y, width, height, color, filled);
    return;
  }

  const uint8_t xMax = x + width - 1;
  const uint8_t yMax = y + height - 1;

  if (filled) {
    for (uint8_t row = 0; row < height; row++) {
      int16_t xStart = x;
      int16_t xEnd = xMax;

      if (row == 0 || row == height - 1) {
        xStart += 2;
        xEnd -= 2;
      } else if (row == 1 || row == height - 2) {
        xStart += 1;
        xEnd -= 1;
      }

      if (xStart <= xEnd) {
        displayDrawLine((uint8_t)xStart, (uint8_t)(y + row), (uint8_t)xEnd,
                        (uint8_t)(y + row), color);
      }
    }
    return;
  }

  // Lados rectos recortados.
  displayDrawLine((uint8_t)(x + 2), y, (uint8_t)(xMax - 2), y, color);
  displayDrawLine((uint8_t)(x + 2), yMax, (uint8_t)(xMax - 2), yMax, color);
  displayDrawLine(x, (uint8_t)(y + 2), x, (uint8_t)(yMax - 2), color);
  displayDrawLine(xMax, (uint8_t)(y + 2), xMax, (uint8_t)(yMax - 2), color);

  // Diagonales de 3 píxeles en esquinas.
  displayDrawPixel((uint8_t)(x + 2), y, color);
  displayDrawPixel((uint8_t)(x + 1), (uint8_t)(y + 1), color);
  displayDrawPixel(x, (uint8_t)(y + 2), color);

  displayDrawPixel((uint8_t)(xMax - 2), y, color);
  displayDrawPixel((uint8_t)(xMax - 1), (uint8_t)(y + 1), color);
  displayDrawPixel(xMax, (uint8_t)(y + 2), color);

  displayDrawPixel(x, (uint8_t)(yMax - 2), color);
  displayDrawPixel((uint8_t)(x + 1), (uint8_t)(yMax - 1), color);
  displayDrawPixel((uint8_t)(x + 2), yMax, color);

  displayDrawPixel(xMax, (uint8_t)(yMax - 2), color);
  displayDrawPixel((uint8_t)(xMax - 1), (uint8_t)(yMax - 1), color);
  displayDrawPixel((uint8_t)(xMax - 2), yMax, color);
}

/// Copia una imagen en el display
/// Se espera que la imagen esté almacenada en forma de columnas little-endian,
/// donde 0 es fondo y 1 es dibujo.
/// \param sprite la imagen a colocar
/// \param x columna de inicio, comenzando desde 0
/// \param y fila de inicio, comenzando desde 0
/// \param color color que representa el "1"
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
void displayPlace(const Sprite sprite, uint8_t x, uint8_t y,
                  DisplayColor color) {
  sh1106_place(sprite, x, y, color);
}

static uint32_t decodeUtf8CodePoint(const char *text, uint16_t *index) {
  const uint8_t c0 = (uint8_t)text[*index];
  if (c0 == '\0') {
    return 0;
  }

  if (c0 < 0x80) {
    (*index)++;
    return c0;
  }

  const uint8_t c1 = (uint8_t)text[*index + 1];
  if ((c0 & 0xE0) == 0xC0 && c1 != '\0' && (c1 & 0xC0) == 0x80) {
    (*index) += 2;
    return ((uint32_t)(c0 & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);
  }

  (*index)++;
  return 0;
}

static uint32_t mapSpanishCodePoint(uint32_t codePoint) {
  switch (codePoint) {
  case 0x00E1:
    return 'a'; // á
  case 0x00E9:
    return 'e'; // é
  case 0x00ED:
    return 'i'; // í
  case 0x00F3:
    return 'o'; // ó
  case 0x00FA:
    return 'u'; // ú
  case 0x00FC:
    return 'u'; // ü
  case 0x00F1:
    return 'n'; // ñ
  case 0x00C1:
    return 'A'; // Á
  case 0x00C9:
    return 'E'; // É
  case 0x00CD:
    return 'I'; // Í
  case 0x00D3:
    return 'O'; // Ó
  case 0x00DA:
    return 'U'; // Ú
  case 0x00DC:
    return 'U'; // Ü
  case 0x00D1:
    return 'N'; // Ñ
  default:
    return codePoint;
  }
}

static const Sprite *spriteFromFont(const SpriteFont *font,
                                    uint32_t codePoint) {
  if (codePoint < font->firstChar || codePoint > font->lastChar) {
    // Intentar a mapear caracteres acentuados comunes a sus equivalentes sin
    // acento
    codePoint = mapSpanishCodePoint(codePoint);
    if (codePoint < font->firstChar || codePoint > font->lastChar) {
      return NULL;
    }
  }

  const Sprite *sprites = (const Sprite *)font->sprites;
  return &sprites[codePoint - font->firstChar];
}

/// Escribe texto con la fuente aseprite_font.
/// \param font fuente a utilizar
/// \param text texto a escribir
/// \param x columna de inicio (esquina superior izquierda)
/// \param y fila de inicio (esquina superior izquierda)
/// \param color el color del texto
/// \note Solo se dibujan caracteres en el rango definido en aseprite_font.
/// \note Los caracteres UTF-8 áéíóúüñÁÉÍÓÚÜÑ usan su glifo en aseprite_font.
/// \note Si una letra excede los límites del display, se detiene la escritura.
void displayText(const SpriteFont *font, const char *text, uint8_t x, uint8_t y,
                 DisplayColor color) {
  if (text == NULL || y >= DISPLAY_HEIGHT) {
    return;
  }

  uint16_t cursorX = x;

  for (uint16_t i = 0; text[i] != '\0';) {
    const uint32_t codePoint = decodeUtf8CodePoint(text, &i);
    if (codePoint == 0) {
      continue;
    }

    const Sprite *sprite = spriteFromFont(font, codePoint);
    if (sprite == NULL) {
      continue;
    }

    if ((uint16_t)y + sprite->height > DISPLAY_HEIGHT ||
        cursorX + sprite->width > DISPLAY_WIDTH) {
      break;
    }

    sh1106_place(*sprite, (uint8_t)cursorX, y, color);
    cursorX += sprite->width;
  }
}

/// Actualiza el display con lo escrito en el buffer
void displayUpdate() { sh1106_update(); }
