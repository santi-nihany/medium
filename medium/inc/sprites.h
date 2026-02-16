/**
 * @file sprites.h
 * @brief Sprite data declarations for OLED display
 *
 * Adapted from branch_ui. Sprite bitmaps stored in const (flash/ROM).
 */

#ifndef _SPRITES_H_
#define _SPRITES_H_

#include "sapi.h"

typedef struct {
    uint8_t width;
    uint8_t height;
    const uint8_t *image;
} Sprite;

/* Splash screen sprite (128x64 full screen) */
extern const Sprite Sprite_0001;
/* Navigation arrows */
extern const Sprite Sprite_0002;
extern const Sprite Sprite_0003;

#endif /* _SPRITES_H_ */
