//===----------------------------------------------------------------------===//
///
/// \file
/// Definiciones para utilzar un display OLED.
///
//===----------------------------------------------------------------------===//

#ifndef DISPLAY_H
#define DISPLAY_H

#include "main.h"
#include "sprites.h"

/// Ancho del display en píxeles
#define DISPLAY_WIDTH 128
/// Alto del display en píxeles
#define DISPLAY_HEIGHT 64

/// Colores del display
typedef enum {
  DISPLAY_BLACK = 0,
  DISPLAY_WHITE = 1,
  DISPLAY_INVERSE = -1,
} DisplayColor;

void displayInit();
void displayDrawPixel(uint8_t x, uint8_t y, DisplayColor color);
void displayDrawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,
                     DisplayColor color);
void displayDrawRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height,
                          DisplayColor color, bool_t filled);
void displayPlace(const Sprite sprite, uint8_t x, uint8_t y);
void displayUpdate();

#endif