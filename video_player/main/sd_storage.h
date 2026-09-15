/**
 * sd_storage.h
 *
 * SD-over-SPI mount, sharing the SPI bus that lcd_driver.c already
 * initialized (see board_config.h for why LCD + SD share SPI2_HOST).
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Mounts the FAT filesystem on the SD card at SD_MOUNT_POINT.
 *
 * IMPORTANT: lcd_driver_init() MUST be called first -- it owns the one
 * and only spi_bus_initialize(SHARED_SPI_HOST, ...) call for this
 * application. This function only attaches an SD SPI device (its own CS
 * pin) to that already-initialized bus; it does not initialize the bus
 * itself. Calling this before lcd_driver_init() will fail.
 */
esp_err_t sd_storage_init(void);

/** True if the card is currently mounted. */
bool sd_storage_is_mounted(void);

/** Convenience check: does VIDEO_FILE_PATH exist and look non-empty? */
esp_err_t sd_storage_check_video_file(const char *path);

void sd_storage_deinit(void);

#ifdef __cplusplus
}
#endif
