#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_log.h"

#include "board_config.h"
#include "lcd_driver.h"
#include "sd_storage.h"
#include "video_player.h"

static const char *TAG = "main";

#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_RED   0xF800

/* Shows an error on both the serial monitor and the LCD, then blocks
 * forever (there is nothing useful this app can do without working
 * hardware, and spinning forever avoids a boot-loop of repeated crashes). */
static void fatal_error(const char *tag, const char *log_msg, const char *lcd_msg)
{
    ESP_LOGE(tag, "%s", log_msg);
    lcd_driver_fill(COLOR_RED);
    lcd_driver_draw_status_text(lcd_msg, COLOR_WHITE, COLOR_RED);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_err_t ret;

    /* NVS is not strictly required by this application (we don't use Wi-Fi,
     * BLE, or any other NVS-backed subsystem), but a handful of ESP-IDF
     * drivers probe it during init and log a warning if it's missing, and
     * it's one line of forward-compatibility if you extend this project
     * later (e.g. remembering the last video played, Wi-Fi OTA, etc). */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init failed (%s) -- continuing without NVS", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "Initializing LCD");
    ret = lcd_driver_init();
    if (ret != ESP_OK) {
        /* We can't show an error ON the LCD if the LCD itself failed to
         * initialize -- this is the one failure mode that is serial-log-only. */
        ESP_LOGE(TAG, "LCD initialization failed (%s) -- halting", esp_err_to_name(ret));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    lcd_driver_fill(COLOR_BLACK);
    lcd_driver_draw_status_text("STARTING", COLOR_WHITE, COLOR_BLACK);

    ESP_LOGI(TAG, "Initializing SD card");
    ret = sd_storage_init();
    if (ret != ESP_OK) {
        fatal_error(TAG, "SD card init/mount failed -- check wiring, card format (FAT32), "
                         "and that the card is inserted", "SD CARD ERROR");
        return; /* unreachable, fatal_error() never returns */
    }

    ret = sd_storage_check_video_file(VIDEO_FILE_PATH);
    if (ret == ESP_ERR_NOT_FOUND) {
        fatal_error(TAG, "Video file not found on SD card", "FILE NOT FOUND");
        return;
    } else if (ret != ESP_OK) {
        fatal_error(TAG, "Video file exists but could not be validated", "VIDEO FILE ERROR");
        return;
    }

    lcd_driver_fill(COLOR_BLACK);

    ret = video_player_start(VIDEO_FILE_PATH);
    if (ret == ESP_ERR_NO_MEM) {
        fatal_error(TAG, "Out of memory allocating video playback buffers -- "
                         "check PSRAM is enabled and detected", "MEMORY ERROR");
        return;
    } else if (ret != ESP_OK) {
        fatal_error(TAG, "Failed to start video playback (bad/unsupported AVI file?)",
                    "UNSUPPORTED FORMAT");
        return;
    }

    ESP_LOGI(TAG, "Video playback running. app_main returning; playback tasks continue in background.");
    /* app_main can return -- the reader_task and decoder_task created by
     * video_player_start() keep running as independent FreeRTOS tasks. */
}
