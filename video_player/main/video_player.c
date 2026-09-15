#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "board_config.h"
#include "avi_reader.h"
#include "jpeg_decoder.h"
#include "lcd_driver.h"
#include "video_player.h"
#include "spi_bus_lock.h"

static const char *TAG = "video_player";

/* A few basic RGB565 colors for status screens. */
#define COLOR_BLACK  0x0000
#define COLOR_WHITE  0xFFFF
#define COLOR_RED    0xF800

#define VIDEO_MAX_CONSECUTIVE_ERRORS  5
#define VIDEO_PATH_MAX_LEN            128

typedef struct {
    uint8_t *buf;
    size_t len;
} frame_msg_t;

static avi_context_t s_avi_ctx;
static char s_video_path[VIDEO_PATH_MAX_LEN];

static QueueHandle_t s_free_queue;   /* holds uint8_t* (empty jpeg buffers)      */
static QueueHandle_t s_filled_queue; /* holds frame_msg_t (buffer + valid length) */

static uint16_t *s_framebuffers[2]; /* double-buffered decoded RGB565 frames */

static esp_err_t reopen_video_file_with_retries(void)
{
    shared_spi_lock_acquire();
    avi_close(&s_avi_ctx);
    shared_spi_lock_release();
    while (true) {
        shared_spi_lock_acquire();
        esp_err_t ret = avi_open(s_video_path, &s_avi_ctx);
        shared_spi_lock_release();
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Failed to reopen %s, retrying in 2s...", s_video_path);
        lcd_driver_fill(COLOR_RED);
        lcd_driver_draw_status_text("VIDEO FILE ERROR", COLOR_WHITE, COLOR_RED);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void reader_task(void *arg)
{
    (void)arg;
    int consecutive_errors = 0;

    while (1) {
        uint8_t *buf = NULL;
        if (xQueueReceive(s_free_queue, &buf, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* Held for the whole SD read -- see spi_bus_lock.h. This is the
         * other half of the fix that also wraps every LCD draw: without
         * it, this read can start on the shared SPI hardware while a
         * frame is still being DMA'd out to the LCD from decoder_task on
         * the other core, which crashes with a low-level SPI HAL assert. */
        shared_spi_lock_acquire();
        size_t len = 0;
        esp_err_t ret = avi_read_next_frame(&s_avi_ctx, buf, VIDEO_MAX_JPEG_FRAME_SIZE, &len);
        shared_spi_lock_release();

        if (ret == ESP_ERR_NOT_FOUND) {
            /* Reached the end of the video -- loop playback from the start. */
            ESP_LOGI(TAG, "End of video reached, looping");
            shared_spi_lock_acquire();
            avi_seek_to_start(&s_avi_ctx);
            shared_spi_lock_release();
            xQueueSend(s_free_queue, &buf, portMAX_DELAY);
            continue;
        }

        if (ret == ESP_ERR_INVALID_SIZE) {
            /* One oversized/corrupt frame -- already skipped by avi_reader,
             * just recycle the buffer and keep going. */
            xQueueSend(s_free_queue, &buf, portMAX_DELAY);
            continue;
        }

        if (ret != ESP_OK) {
            consecutive_errors++;
            ESP_LOGE(TAG, "SD read error (%d/%d consecutive)",
                     consecutive_errors, VIDEO_MAX_CONSECUTIVE_ERRORS);
            xQueueSend(s_free_queue, &buf, portMAX_DELAY);

            if (consecutive_errors >= VIDEO_MAX_CONSECUTIVE_ERRORS) {
                ESP_LOGE(TAG, "Too many consecutive SD errors -- attempting to reopen the file");
                reopen_video_file_with_retries();
                consecutive_errors = 0;
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            continue;
        }

        consecutive_errors = 0;
        frame_msg_t msg = { .buf = buf, .len = len };
        xQueueSend(s_filled_queue, &msg, portMAX_DELAY);
    }
}

static void decoder_task(void *arg)
{
    (void)arg;
    int current = 0;
    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t interval_ms = s_avi_ctx.frame_interval_ms ? s_avi_ctx.frame_interval_ms
                                                        : VIDEO_DEFAULT_FRAME_INTERVAL_MS;
    TickType_t frame_ticks = pdMS_TO_TICKS(interval_ms);

    while (1) {
        frame_msg_t msg;
        xQueueReceive(s_filled_queue, &msg, portMAX_DELAY);

        uint16_t *fb = s_framebuffers[current];
        esp_err_t ret = jpeg_decoder_decode_frame(msg.buf, msg.len, fb);

        /* Compressed buffer's payload has been consumed either way -- give
         * it back to the reader immediately so it can keep reading ahead. */
        xQueueSend(s_free_queue, &msg.buf, portMAX_DELAY);

        if (ret == ESP_OK) {
            lcd_driver_draw_fullscreen(fb);
            current ^= 1; /* swap to the other framebuffer for next frame */
        } else {
            ESP_LOGW(TAG, "Dropped a frame due to a decode error");
        }

        vTaskDelayUntil(&last_wake_time, frame_ticks);
    }
}

esp_err_t video_player_start(const char *path)
{
    strncpy(s_video_path, path, sizeof(s_video_path) - 1);
    s_video_path[sizeof(s_video_path) - 1] = '\0';

    esp_err_t ret = avi_open(path, &s_avi_ctx);
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_avi_ctx.width != LCD_WIDTH || s_avi_ctx.height != LCD_HEIGHT) {
        ESP_LOGW(TAG, "Video is %ux%u but the LCD is %dx%d -- decoding will fail. "
                      "Re-encode with the ffmpeg command in the README to match "
                      "the LCD's native resolution exactly.",
                 (unsigned)s_avi_ctx.width, (unsigned)s_avi_ctx.height, LCD_WIDTH, LCD_HEIGHT);
    }

    ret = jpeg_decoder_init(LCD_WIDTH, LCD_HEIGHT);
    if (ret != ESP_OK) {
        avi_close(&s_avi_ctx);
        return ret;
    }

    /* Decoder output buffers must be 16-byte aligned on ESP32-S3 (esp_new_jpeg
     * uses SIMD instructions on this chip that require it). PSRAM-backed so
     * two full 240x320x16bpp frames (~300 KB) don't strain internal SRAM. */
    size_t fb_size = (size_t)LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t);
    for (int i = 0; i < 2; i++) {
        s_framebuffers[i] = heap_caps_aligned_alloc(16, fb_size, MALLOC_CAP_SPIRAM);
        if (s_framebuffers[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate framebuffer %d (%u bytes) from PSRAM", i, (unsigned)fb_size);
            return ESP_ERR_NO_MEM;
        }
        memset(s_framebuffers[i], 0, fb_size);
    }

    s_free_queue = xQueueCreate(VIDEO_NUM_JPEG_BUFFERS, sizeof(uint8_t *));
    s_filled_queue = xQueueCreate(VIDEO_NUM_JPEG_BUFFERS, sizeof(frame_msg_t));
    if (s_free_queue == NULL || s_filled_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create playback queues");
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < VIDEO_NUM_JPEG_BUFFERS; i++) {
        uint8_t *buf = heap_caps_malloc(VIDEO_MAX_JPEG_FRAME_SIZE, MALLOC_CAP_SPIRAM);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate compressed-frame buffer %d (%u bytes) from PSRAM",
                     i, VIDEO_MAX_JPEG_FRAME_SIZE);
            return ESP_ERR_NO_MEM;
        }
        xQueueSend(s_free_queue, &buf, 0);
    }

    BaseType_t task_ret;
    task_ret = xTaskCreatePinnedToCore(reader_task, "video_reader", 4096, NULL, 5, NULL, 0);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create reader task");
        return ESP_ERR_NO_MEM;
    }
    task_ret = xTaskCreatePinnedToCore(decoder_task, "video_decoder", 8192, NULL, 6, NULL, 1);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create decoder task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Playback started: %s", path);
    return ESP_OK;
}
