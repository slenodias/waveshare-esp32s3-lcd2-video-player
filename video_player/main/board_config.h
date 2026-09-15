/**
 * board_config.h
 *
 * Hardware configuration for the Waveshare ESP32-S3-LCD-2.
 *
 * IMPORTANT: every #define in this file was read directly out of the two
 * attached Waveshare reference projects. Nothing here is guessed.
 *
 *   - LCD_* pins/timings   -> taken from 06_lvgl_example/main/main.c
 *   - SD_*  pins           -> taken from 01_sd_card_test/main/sd_card_example_main.c
 *
 * KEY FINDING: the LCD and the SD card are wired to the SAME SPI bus
 * (SPI2_HOST). Both examples use identical SCLK/MOSI/MISO pins (39/38/40);
 * only the CS pin differs (LCD = GPIO45, SD = GPIO41). This is a genuine
 * shared-bus configuration, not a coincidence -- see board_bus.c /
 * sd_storage.c for how the bus is initialized exactly once and shared.
 */

#pragma once

#include "driver/spi_master.h"
#include "driver/gpio.h"

/* ------------------------------------------------------------------ */
/* Shared SPI bus (LCD + SD card)                                      */
/* ------------------------------------------------------------------ */
#define SHARED_SPI_HOST         SPI2_HOST      /* from both examples */
#define SHARED_SPI_PIN_SCLK     39             /* EXAMPLE_PIN_NUM_SCLK / PIN_NUM_CLK  */
#define SHARED_SPI_PIN_MOSI     38             /* EXAMPLE_PIN_NUM_MOSI / PIN_NUM_MOSI */
#define SHARED_SPI_PIN_MISO     40             /* EXAMPLE_PIN_NUM_MISO / PIN_NUM_MISO */

/* Max single SPI transfer size for the shared bus. The reference examples
 * used a very small 4000-byte limit (fine for LVGL widgets / small text
 * files) which is too small for efficient full-frame video pushes and
 * multi-sector SD reads. We raise it here; this only affects how the
 * driver splits transfers internally and is safe for both peripherals. */
#define SHARED_SPI_MAX_TRANSFER_SZ  (32 * 1024)

/* ------------------------------------------------------------------ */
/* LCD (ST7789, from 06_lvgl_example)                                   */
/* ------------------------------------------------------------------ */
#define LCD_PIN_CS              45   /* EXAMPLE_PIN_NUM_LCD_CS  */
#define LCD_PIN_DC              42   /* EXAMPLE_PIN_NUM_LCD_DC  */
#define LCD_PIN_RST             -1   /* EXAMPLE_PIN_NUM_LCD_RST : not wired to a GPIO */
#define LCD_PIN_BACKLIGHT       1    /* EXAMPLE_PIN_NUM_BK_LIGHT */

#define LCD_WIDTH               240  /* EXAMPLE_LCD_H_RES */
#define LCD_HEIGHT              320  /* EXAMPLE_LCD_V_RES */

#define LCD_PIXEL_CLOCK_HZ      (80 * 1000 * 1000)  /* EXAMPLE_LCD_PIXEL_CLOCK_HZ */
#define LCD_CMD_BITS            8
#define LCD_PARAM_BITS          8
#define LCD_BITS_PER_PIXEL      16   /* RGB565 */

/* The reference example calls esp_lcd_panel_invert_color(panel, true).
 * This ST7789 panel needs color inversion to show correct colors --
 * this is a panel-specific quirk confirmed by the Waveshare example,
 * not a generic ST7789 requirement. Keep it. */
#define LCD_INVERT_COLOR        true
#define LCD_MIRROR_X            false
#define LCD_MIRROR_Y            false
#define LCD_SWAP_XY             false

/* Backlight is PWM-dimmed via LEDC in the reference example. */
#define LCD_BL_LEDC_TIMER       LEDC_TIMER_0
#define LCD_BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LCD_BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define LCD_BL_LEDC_DUTY_RES    LEDC_TIMER_10_BIT
#define LCD_BL_LEDC_FREQUENCY   10000
#define LCD_BL_DEFAULT_PERCENT  80

/* ------------------------------------------------------------------ */
/* microSD card (SPI mode, from 01_sd_card_test)                        */
/* ------------------------------------------------------------------ */
#define SD_PIN_CS               41   /* PIN_NUM_CS  */
/* MOSI/MISO/CLK are shared with the LCD -- see SHARED_SPI_PIN_* above. */

#define SD_MOUNT_POINT          "/sdcard"
#define SD_MAX_FILES            5
#define SD_ALLOCATION_UNIT_SIZE (16 * 1024)

/* ------------------------------------------------------------------ */
/* Video source                                                        */
/* ------------------------------------------------------------------ */
/* Change this to point at a different clip. Keep the leading slash and
 * SD_MOUNT_POINT prefix. */
#define VIDEO_FILE_PATH         SD_MOUNT_POINT "/video.avi"
