/**
 * lcd_driver.h
 *
 * Direct (non-LVGL) ST7789 driver for the Waveshare ESP32-S3-LCD-2.
 *
 * WHY NOT LVGL:
 * The attached 06_lvgl_example uses LVGL, but LVGL is a *retained-mode UI
 * toolkit*: every flush goes through its dirty-rectangle tracking, object
 * tree, and an extra buffer-copy/round-trip through its render loop. For a
 * full-screen video player that redraws all 240x320 pixels on every single
 * frame, none of that machinery buys anything -- there are no widgets, no
 * partial redraws, no input events. It only adds RAM (two full LVGL draw
 * buffers) and CPU overhead we cannot afford at video framerates on an
 * ESP32-S3. We instead talk to the same esp_lcd_panel_* driver the LVGL
 * example uses, but push our own decoded RGB565 buffer straight to the
 * panel via esp_lcd_panel_draw_bitmap(), which is exactly what LVGL's
 * flush callback does internally anyway -- we just skip the middleman.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes the shared SPI bus (SPI2_HOST, used by both the LCD and the
 * SD card -- see board_config.h), installs the ST7789 panel driver using
 * the exact init sequence from 06_lvgl_example, and turns on the backlight.
 *
 * Must be called exactly once, before sd_storage_init().
 */
esp_err_t lcd_driver_init(void);

/**
 * Blocking full-screen draw of a pre-rendered RGB565 buffer
 * (LCD_WIDTH * LCD_HEIGHT * 2 bytes, row-major, native ST7789 byte order).
 *
 * This function returns only after the previous DMA transfer (if any) has
 * completed, so the caller is free to reuse/overwrite the PREVIOUS buffer
 * it handed in as soon as this call returns -- callers should therefore
 * ping-pong between (at least) two buffers to overlap decode with DMA.
 */
esp_err_t lcd_driver_draw_fullscreen(const uint16_t *rgb565_buffer);

/** Sets backlight brightness, 0-100%. */
void lcd_driver_set_brightness(uint8_t percent);

/**
 * Fills the whole screen with a single RGB565 color. Cheap way to show a
 * status/error background without needing a full frame buffer.
 */
esp_err_t lcd_driver_fill(uint16_t rgb565_color);

/**
 * Draws a short ASCII status/error string, centered, on top of whatever is
 * currently on screen, using the tiny built-in bitmap font in
 * simple_font.h. Intended for boot-time / fatal error messages only (see
 * simple_font.h for supported characters and README.md for the design
 * rationale of not pulling in LVGL/a font engine for this).
 */
esp_err_t lcd_driver_draw_status_text(const char *text, uint16_t fg_color, uint16_t bg_color);

#ifdef __cplusplus
}
#endif
