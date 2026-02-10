//===----------------------------------------------------------------------===//
///
/// \file
/// Librería para utilizar un display OLED por I2C controlado por el SH1106.
///
//===----------------------------------------------------------------------===//

#include "sh1106.h"

/// Dirección I2C del SH1106
#define SH1106_I2C_ADDR 0x3C

/// Commandos disponibles para el SH1106
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

static uint8_t buffer[SH1106_BUFFER_SIZE] = {0};

/// Enviar comando al SH1106
static void sh1106_command(SH1106_Command cmd) {
  uint8_t data[2] = {0x00, cmd};
  i2cWrite(I2C0, SH1106_I2C_ADDR, data, 2, TRUE);
}

/// Inicialización del SH1106
void sh1106_init(void) {
  // Secuencia de inicialización
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

/// Actualiza el display con lo escrito en el buffer
void sh1106_update() {
  uint8_t page;
  // SH1106 usa paginación, no addressing horizontal
  // Tiene 132 columnas pero solo mostramos 128
  for (page = 0; page < 8; page++) {
    sh1106_command(SH1106_SETPAGEADDR | page);
    // Offset de 2 columnas para centrar en el display
    sh1106_command(SH1106_SETLOWCOLUMN | 0x02);
    sh1106_command(SH1106_SETHIGHCOLUMN | 0x00);

    // Enviar una página de datos
    uint8_t data[SH1106_WIDTH + 1];
    data[0] = 0x40; // Co=0, D/C=1
    memcpy(&data[1], &buffer[page * SH1106_WIDTH], SH1106_WIDTH);

    i2cWrite(I2C0, SH1106_I2C_ADDR, data, SH1106_WIDTH + 1, TRUE);
  }
}

/// Pinta toda la pantalla del color especificado
void sh1106_fill(SH1106_Color color) {
  switch (color) {
  case SH1106_WHITE:
    for (uint16_t i = 0; i < SH1106_BUFFER_SIZE; i++) {
      buffer[i] = 0xFF;
    }
    break;
  case SH1106_BLACK:
    for (uint16_t i = 0; i < SH1106_BUFFER_SIZE; i++) {
      buffer[i] = 0x00;
    }
    break;
  case SH1106_INVERSE:
    for (uint16_t i = 0; i < SH1106_BUFFER_SIZE; i++) {
      buffer[i] ^= 0xFF;
    }
    break;
  }
}

/// Cambia el color de un píxel
/// \param x columna del píxel, comenzando desde 0
/// \param y fila del píxel, comenzando desde 0
void sh1106_draw(uint8_t x, uint8_t y, SH1106_Color color) {
  if (x < 0 || x >= SH1106_WIDTH || y < 0 || y >= SH1106_HEIGHT)
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
void sh1106_place(const Sprite sprite, uint8_t x, uint8_t y) {
  uint8_t shift = y & 7;
  uint8_t pages = sprite.height / 8;
  uint8_t page, col;

  // Separar la imagen en páginas de 8 píxeles y columnas
  for (page = 0; page < pages; page++) {
    for (col = 0; col < sprite.width; col++) {
      uint8_t imageByte = sprite.image[page * sprite.width + col];
      uint8_t screenX = col + x;
      uint8_t screenPage = page + y / 8;
      if (screenX >= SH1106_WIDTH || screenPage >= SH1106_HEIGHT / 8)
        continue;

      // Guardar primera parte del byte en la página
      buffer[screenPage * SH1106_WIDTH + screenX] =
          (buffer[screenPage * SH1106_WIDTH + screenX] & ~(0xFF << shift)) |
          (imageByte << shift);

      // Guardar segunda parte del byte en la página siguiente
      if (shift != 0) {
        screenPage++;
        if (screenPage >= SH1106_HEIGHT / 8)
          continue;
        buffer[screenPage * SH1106_WIDTH + screenX] =
            (buffer[screenPage * SH1106_WIDTH + screenX] &
             ~(0xFF >> (8 - shift))) |
            (imageByte >> (8 - shift));
      }
    }
  }

  // Guardar últimos bits en `page`
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