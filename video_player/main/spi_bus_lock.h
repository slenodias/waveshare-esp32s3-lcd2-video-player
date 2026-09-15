/**
 * spi_bus_lock.h
 *
 * The LCD and the SD card share one physical SPI bus (SPI2_HOST) with two
 * different CS lines. The ESP-IDF driver normally arbitrates between
 * devices on a shared bus, but the LCD path here uses asynchronous
 * DMA transfers (esp_lcd_panel_draw_bitmap() returns before the transfer
 * finishes) while the SD card path uses polling transfers issued from a
 * different core. Letting those genuinely overlap can hit a low-level
 * assert in the SPI HAL (spi_hal_setup_trans: spi_ll_get_running_cmd(hw)
 * == 0) because a new transaction gets started on the hardware while the
 * previous one hasn't finished clearing its state.
 *
 * This tiny mutex gives the application an explicit guarantee: only one
 * of {a full LCD draw, a full SD read} is ever in flight on the shared
 * bus at a time. lcd_driver.c and video_player.c's reader_task both
 * acquire it around their SPI activity.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Creates the underlying mutex. Safe to call more than once. Call this
 * before any task might call shared_spi_lock_acquire(). */
void shared_spi_lock_init(void);

/** Blocks until exclusive access to the shared SPI bus is available. */
void shared_spi_lock_acquire(void);

/** Releases exclusive access previously obtained via shared_spi_lock_acquire(). */
void shared_spi_lock_release(void);

#ifdef __cplusplus
}
#endif
