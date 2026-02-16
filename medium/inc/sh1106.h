/**
 * @file sh1106.h
 * @brief SH1106 OLED display driver (I2C0, 128x64)
 *
 * Adapted from branch_ui. Provides init, pixel drawing, sprite placement,
 * fill, and display update via I2C.
 */

#ifndef _SH1106_H_
#define _SH1106_H_

#include "sapi.h"
#include "sprites.h"

#define SH1106_WIDTH  128
#define SH1106_HEIGHT  64
#define SH1106_BUFFER_SIZE (SH1106_WIDTH * SH1106_HEIGHT / 8)

typedef enum {
    SH1106_BLACK   =  0,
    SH1106_WHITE   =  1,
    SH1106_INVERSE = -1,
} SH1106_Color;

void sh1106_init(void);
void sh1106_draw(uint8_t x, uint8_t y, SH1106_Color color);
void sh1106_fill(SH1106_Color color);
void sh1106_place(const Sprite sprite, uint8_t x, uint8_t y);
void sh1106_update(void);

/* Text rendering (simple 5x7 font) */
void sh1106_drawChar(uint8_t x, uint8_t y, char c, SH1106_Color color);
void sh1106_drawString(uint8_t x, uint8_t y, const char *str, SH1106_Color color);

/* Drawing primitives */
void sh1106_drawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, SH1106_Color color);
void sh1106_drawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, SH1106_Color color, bool_t filled);

#endif /* _SH1106_H_ */
