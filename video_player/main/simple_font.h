/**
 * simple_font.h
 *
 * A deliberately tiny built-in 5x7 bitmap font, used ONLY for short
 * uppercase status/error messages on the LCD (e.g. "SD CARD ERROR").
 *
 * WHY THIS EXISTS INSTEAD OF LVGL:
 * The user requirement is "display a useful status message on the LCD"
 * for fatal errors. Pulling in LVGL (or any font-rendering library) just
 * for a handful of boot-time error strings would reintroduce the exact
 * overhead this project avoids by not using LVGL for video (see the
 * comment at the top of lcd_driver.h). This font covers only the
 * characters needed for status text: space, A-Z, 0-9, and a few
 * punctuation marks. It is NOT a general-purpose text renderer.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIMPLE_FONT_GLYPH_WIDTH  5
#define SIMPLE_FONT_GLYPH_HEIGHT 7

/**
 * Draws `text` (uppercase letters, digits, space, and . : / - only --
 * anything else is rendered as a blank glyph) into `buf`, which is a
 * buf_w x buf_h RGB565 raster, starting at pixel (x0, y0). `scale`
 * upscales each font pixel to a scale x scale block.
 */
void simple_font_draw_string(uint16_t *buf, int buf_w, int buf_h,
                              int x0, int y0, const char *text,
                              int scale, uint16_t color);

#ifdef __cplusplus
}
#endif
