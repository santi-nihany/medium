//===----------------------------------------------------------------------===//
///
/// \file
/// Definiciones de sprites y otros gráficos.
///
//===----------------------------------------------------------------------===//

#ifndef SPRITES_H
#define SPRITES_H

#include "main.h"

typedef struct {
  uint8_t width;
  uint8_t height;
  const uint8_t *image;
} Sprite;

extern const Sprite sprite_background;
extern const Sprite sprite_title;
extern const Sprite sprite_ir;
extern const Sprite sprite_rf;
extern const Sprite sprite_lselector;
extern const Sprite sprite_rselector;

#define ASEPRITE_MINI_START 0x20
#define ASEPRITE_MINI_END 0x7a

extern const Sprite aseprite_mini[ASEPRITE_MINI_END - ASEPRITE_MINI_START + 1];

#endif
