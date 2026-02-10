//===----------------------------------------------------------------------===//
///
/// \file
/// Definiciones para utilzar un display OLED por I2C controlado por el SH1106.
///
//===----------------------------------------------------------------------===//

#ifndef SH1106_H
#define SH1106_H

#include "main.h"
#include "sprites.h"

/// Ancho del display en píxeles
#define SH1106_WIDTH 128
/// Alto del display en píxeles
#define SH1106_HEIGHT 64
/// Tamaño del buffer que representa al display (páginas de 8 píxeles)
#define SH1106_BUFFER_SIZE (SH1106_WIDTH * SH1106_HEIGHT / 8)

/// Colores del display
typedef enum {
  SH1106_BLACK = 0,
  SH1106_WHITE = 1,
  SH1106_INVERSE = -1,
} SH1106_Color;

void sh1106_init();
void sh1106_draw(uint8_t x, uint8_t y, SH1106_Color color);
void sh1106_fill(SH1106_Color color);
void sh1106_place(const Sprite sprite, uint8_t x, uint8_t y);
void sh1106_update();

#endif