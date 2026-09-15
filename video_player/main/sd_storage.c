#include <sys/stat.h>
#include <string.h>

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_log.h"

#include "board_config.h"
#include "sd_storage.h"

static const char *TAG = "sd_storage";

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

esp_err_t sd_storage_init(void)
{
    ESP_LOGI(TAG, "Mounting SD card (SPI mode) at %s", SD_MOUNT_POINT);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = SD_ALLOCATION_UNIT_SIZE,
    };

    /* NOTE: we deliberately do NOT call spi_bus_initialize() here.
     * lcd_driver_init() already initialized SHARED_SPI_HOST (SPI2_HOST)
     * with buses wide enough for both peripherals. esp_vfs_fat_sdspi_mount()
     * below only attaches a new SPI *device* (this card, on its own CS
     * line, SD_PIN_CS) to that existing bus -- this is the officially
     * supported way to share one SPI bus between an LCD and an SD card. */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SHARED_SPI_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = host.slot;

    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config,
                                             &mount_config, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. Card may need formatting, "
                          "or the FAT partition on it is corrupt/missing.");
        } else if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "SD SPI mount called before the shared SPI bus was "
                          "initialized -- call lcd_driver_init() first.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Check wiring and "
                          "that SD lines have pull-up resistors.", esp_err_to_name(ret));
        }
        s_mounted = false;
        return ret;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted:");
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

bool sd_storage_is_mounted(void)
{
    return s_mounted;
}

esp_err_t sd_storage_check_video_file(const char *path)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "Video file not found: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    if (st.st_size <= 0) {
        ESP_LOGE(TAG, "Video file is empty: %s", path);
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "Found video file %s (%ld bytes)", path, (long)st.st_size);
    return ESP_OK;
}

void sd_storage_deinit(void)
{
    if (s_mounted) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        s_mounted = false;
        s_card = NULL;
        ESP_LOGI(TAG, "SD card unmounted");
    }
    /* We do NOT call spi_bus_free() here -- the LCD is still using the bus. */
}
