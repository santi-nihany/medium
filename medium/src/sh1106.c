/**
 * @file sh1106.c
 * @brief SH1106 OLED display driver implementation
 *
 * Adapted from branch_ui. I2C-based SH1106 driver with pixel drawing,
 * sprite placement, text rendering (5x7 font), and display update.
 */

#include "sh1106.h"
#include <string.h>
#include <stdlib.h>

/*==================[I2C address]===========================================*/

#define SH1106_I2C_ADDR 0x3C

/*==================[commands]==============================================*/

typedef enum {
    SH1106_SETCONTRAST = 0x81,
    SH1106_DISPLAYALLON_RESUME = 0xA4,
    SH1106_DISPLAYALLON = 0xA5,
    SH1106_NORMALDISPLAY = 0xA6,
    SH1106_INVERTDISPLAY = 0xA7,
    SH1106_DISPLAYOFF = 0xAE,
    SH1106_DISPLAYON = 0xAF,
    SH1106_SETDISPLAYOFFSET = 0xD3,
    SH1106_SETCOMPINS = 0xDA,
    SH1106_SETVCOMDETECT = 0xDB,
    SH1106_SETDISPLAYCLOCKDIV = 0xD5,
    SH1106_SETPRECHARGE = 0xD9,
    SH1106_SETMULTIPLEX = 0xA8,
    SH1106_SETLOWCOLUMN = 0x00,
    SH1106_SETHIGHCOLUMN = 0x10,
    SH1106_SETSTARTLINE = 0x40,
    SH1106_COMSCANINC = 0xC0,
    SH1106_COMSCANDEC = 0xC8,
    SH1106_SEGREMAP = 0xA0,
    SH1106_CHARGEPUMP = 0x8D,
    SH1106_SETPAGEADDR = 0xB0
} SH1106_Command;

/*==================[framebuffer]===========================================*/

static uint8_t buffer[SH1106_BUFFER_SIZE] = {0};

/*==================[5x7 font]=============================================*/

/* Basic ASCII font 32-126, 5 bytes per character (5 columns, 7 rows) */
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* ! */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* " */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* # */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* $ */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* % */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* & */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* ' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* ( */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* ) */
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, /* * */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* + */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* , */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* ; */
    {0x00, 0x08, 0x14, 0x22, 0x41}, /* < */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* = */
    {0x41, 0x22, 0x14, 0x08, 0x00}, /* > */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* ? */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* @ */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x01, 0x01}, /* F */
    {0x3E, 0x41, 0x41, 0x51, 0x32}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x03, 0x04, 0x78, 0x04, 0x03}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
    {0x00, 0x00, 0x7F, 0x41, 0x41}, /* [ */
    {0x02, 0x04, 0x08, 0x10, 0x20}, /* \ */
    {0x41, 0x41, 0x7F, 0x00, 0x00}, /* ] */
    {0x04, 0x02, 0x01, 0x02, 0x04}, /* ^ */
    {0x40, 0x40, 0x40, 0x40, 0x40}, /* _ */
    {0x00, 0x01, 0x02, 0x04, 0x00}, /* ` */
    {0x20, 0x54, 0x54, 0x54, 0x78}, /* a */
    {0x7F, 0x48, 0x44, 0x44, 0x38}, /* b */
    {0x38, 0x44, 0x44, 0x44, 0x20}, /* c */
    {0x38, 0x44, 0x44, 0x48, 0x7F}, /* d */
    {0x38, 0x54, 0x54, 0x54, 0x18}, /* e */
    {0x08, 0x7E, 0x09, 0x01, 0x02}, /* f */
    {0x08, 0x14, 0x54, 0x54, 0x3C}, /* g */
    {0x7F, 0x08, 0x04, 0x04, 0x78}, /* h */
    {0x00, 0x44, 0x7D, 0x40, 0x00}, /* i */
    {0x20, 0x40, 0x44, 0x3D, 0x00}, /* j */
    {0x00, 0x7F, 0x10, 0x28, 0x44}, /* k */
    {0x00, 0x41, 0x7F, 0x40, 0x00}, /* l */
    {0x7C, 0x04, 0x18, 0x04, 0x78}, /* m */
    {0x7C, 0x08, 0x04, 0x04, 0x78}, /* n */
    {0x38, 0x44, 0x44, 0x44, 0x38}, /* o */
    {0x7C, 0x14, 0x14, 0x14, 0x08}, /* p */
    {0x08, 0x14, 0x14, 0x18, 0x7C}, /* q */
    {0x7C, 0x08, 0x04, 0x04, 0x08}, /* r */
    {0x48, 0x54, 0x54, 0x54, 0x20}, /* s */
    {0x04, 0x3F, 0x44, 0x40, 0x20}, /* t */
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, /* u */
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, /* v */
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, /* w */
    {0x44, 0x28, 0x10, 0x28, 0x44}, /* x */
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, /* y */
    {0x44, 0x64, 0x54, 0x4C, 0x44}, /* z */
    {0x00, 0x08, 0x36, 0x41, 0x00}, /* { */
    {0x00, 0x00, 0x7F, 0x00, 0x00}, /* | */
    {0x00, 0x41, 0x36, 0x08, 0x00}, /* } */
    {0x08, 0x08, 0x2A, 0x1C, 0x08}, /* ~ */
};

/*==================[private functions]=====================================*/

static void sh1106_command(SH1106_Command cmd)
{
    uint8_t data[2] = {0x00, cmd};
    i2cWrite(I2C0, SH1106_I2C_ADDR, data, 2, TRUE);
}

/*==================[public functions]======================================*/

void sh1106_init(void)
{
    sh1106_command(SH1106_DISPLAYOFF);
    sh1106_command(SH1106_SETDISPLAYCLOCKDIV);
    sh1106_command(0x80);
    sh1106_command(SH1106_SETMULTIPLEX);
    sh1106_command(SH1106_HEIGHT - 1);
    sh1106_command(SH1106_SETDISPLAYOFFSET);
    sh1106_command(0x0);
    sh1106_command(SH1106_SETSTARTLINE | 0x0);
    sh1106_command(SH1106_CHARGEPUMP);
    sh1106_command(0x14);
    sh1106_command(SH1106_SEGREMAP | 0x1);
    sh1106_command(SH1106_COMSCANDEC);
    sh1106_command(SH1106_SETCOMPINS);
    sh1106_command(0x12);
    sh1106_command(SH1106_SETCONTRAST);
    sh1106_command(0xCF);
    sh1106_command(SH1106_SETPRECHARGE);
    sh1106_command(0xF1);
    sh1106_command(SH1106_SETVCOMDETECT);
    sh1106_command(0x40);
    sh1106_command(SH1106_DISPLAYALLON_RESUME);
    sh1106_command(SH1106_NORMALDISPLAY);
    sh1106_command(SH1106_DISPLAYON);

    sh1106_fill(SH1106_BLACK);
    sh1106_update();
}

void sh1106_update(void)
{
    uint8_t page;
    for (page = 0; page < 8; page++) {
        sh1106_command(SH1106_SETPAGEADDR | page);
        sh1106_command(SH1106_SETLOWCOLUMN | 0x02);
        sh1106_command(SH1106_SETHIGHCOLUMN | 0x00);

        uint8_t data[SH1106_WIDTH + 1];
        data[0] = 0x40;
        memcpy(&data[1], &buffer[page * SH1106_WIDTH], SH1106_WIDTH);
        i2cWrite(I2C0, SH1106_I2C_ADDR, data, SH1106_WIDTH + 1, TRUE);
    }
}

void sh1106_fill(SH1106_Color color)
{
    switch (color) {
    case SH1106_WHITE:
        memset(buffer, 0xFF, SH1106_BUFFER_SIZE);
        break;
    case SH1106_BLACK:
        memset(buffer, 0x00, SH1106_BUFFER_SIZE);
        break;
    case SH1106_INVERSE:
        for (uint16_t i = 0; i < SH1106_BUFFER_SIZE; i++) {
            buffer[i] ^= 0xFF;
        }
        break;
    }
}

void sh1106_draw(uint8_t x, uint8_t y, SH1106_Color color)
{
    if (x >= SH1106_WIDTH || y >= SH1106_HEIGHT)
        return;

    switch (color) {
    case SH1106_WHITE:
        buffer[x + (y / 8) * SH1106_WIDTH] |= (1 << (y & 7));
        break;
    case SH1106_BLACK:
        buffer[x + (y / 8) * SH1106_WIDTH] &= ~(1 << (y & 7));
        break;
    case SH1106_INVERSE:
        buffer[x + (y / 8) * SH1106_WIDTH] ^= (1 << (y & 7));
        break;
    }
}

void sh1106_place(const Sprite sprite, uint8_t x, uint8_t y)
{
    uint8_t shift = y & 7;
    uint8_t pages = sprite.height / 8;
    uint8_t page, col;

    for (page = 0; page < pages; page++) {
        for (col = 0; col < sprite.width; col++) {
            uint8_t imageByte = sprite.image[page * sprite.width + col];
            uint8_t screenX = col + x;
            uint8_t screenPage = page + y / 8;
            if (screenX >= SH1106_WIDTH || screenPage >= SH1106_HEIGHT / 8)
                continue;

            buffer[screenPage * SH1106_WIDTH + screenX] =
                (buffer[screenPage * SH1106_WIDTH + screenX] & ~(0xFF << shift)) |
                (imageByte << shift);

            if (shift != 0) {
                screenPage++;
                if (screenPage >= SH1106_HEIGHT / 8) continue;
                buffer[screenPage * SH1106_WIDTH + screenX] =
                    (buffer[screenPage * SH1106_WIDTH + screenX] &
                     ~(0xFF >> (8 - shift))) |
                    (imageByte >> (8 - shift));
            }
        }
    }

    uint8_t lastBits = sprite.height % 8;
    if (lastBits != 0) {
        for (col = 0; col < sprite.width; col++) {
            uint8_t imageByte = sprite.image[page * sprite.width + col];
            uint8_t screenX = col + x;
            uint8_t screenPage = page + y / 8;
            if (screenX >= SH1106_WIDTH || screenPage >= SH1106_HEIGHT / 8)
                continue;

            buffer[screenPage * SH1106_WIDTH + screenX] =
                (buffer[screenPage * SH1106_WIDTH + screenX] &
                 ~(~(0xFF << lastBits) << shift)) |
                ((imageByte & ~(0xFF << lastBits)) << shift);
        }
    }
}

/*==================[text rendering]========================================*/

void sh1106_drawChar(uint8_t x, uint8_t y, char c, SH1106_Color color)
{
    if (c < 32 || c > 126) c = '?';
    uint8_t idx = c - 32;

    for (uint8_t col = 0; col < 5; col++) {
        uint8_t line = font5x7[idx][col];
        for (uint8_t row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                sh1106_draw(x + col, y + row, color);
            }
        }
    }
}

void sh1106_drawString(uint8_t x, uint8_t y, const char *str, SH1106_Color color)
{
    while (*str) {
        if (x + 6 > SH1106_WIDTH) {
            x = 0;
            y += 8;
        }
        if (y + 7 > SH1106_HEIGHT) break;
        sh1106_drawChar(x, y, *str, color);
        x += 6;  /* 5 pixel char + 1 pixel spacing */
        str++;
    }
}

/*==================[drawing primitives]====================================*/

void sh1106_drawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1,
                     SH1106_Color color)
{
    int8_t steep = abs(y1 - y0) > abs(x1 - x0);
    uint8_t tmp;

    if (steep) {
        tmp = x0; x0 = y0; y0 = tmp;
        tmp = x1; x1 = y1; y1 = tmp;
    }
    if (x0 > x1) {
        tmp = x0; x0 = x1; x1 = tmp;
        tmp = y0; y0 = y1; y1 = tmp;
    }

    uint8_t dx = x1 - x0;
    uint8_t dy = abs(y1 - y0);
    int8_t err = dx / 2;
    int8_t ystep = (y0 < y1) ? 1 : -1;

    for (; x0 <= x1; x0++) {
        if (steep) {
            sh1106_draw(y0, x0, color);
        } else {
            sh1106_draw(x0, y0, color);
        }
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

void sh1106_drawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                     SH1106_Color color, bool_t filled)
{
    if (filled) {
        for (uint8_t i = 0; i < w; i++) {
            sh1106_drawLine(x + i, y, x + i, y + h - 1, color);
        }
    } else {
        sh1106_drawLine(x, y, x + w - 1, y, color);
        sh1106_drawLine(x, y, x, y + h - 1, color);
        sh1106_drawLine(x + w - 1, y + h - 1, x, y + h - 1, color);
        sh1106_drawLine(x + w - 1, y + h - 1, x + w - 1, y, color);
    }
}

/*==================[end of file]===========================================*/
