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

extern const Sprite Sprite_0001;
extern const Sprite Sprite_0002;
extern const Sprite Sprite_0003;

#endif