#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "board_config.h"
#include "lcd_driver.h"
#include "simple_font.h"
#include "spi_bus_lock.h"

static const char *TAG = "lcd_driver";

static esp_lcd_panel_handle_t s_panel_handle = NULL;
static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static SemaphoreHandle_t s_flush_done_sem = NULL;

/* Called from the SPI ISR when a color-data transfer to the panel finishes.
 * Mirrors example_notify_lvgl_flush_ready() from 06_lvgl_example, but signals
 * a plain semaphore instead of an LVGL display driver. */
static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                           esp_lcd_panel_io_event_data_t *edata,
                                           void *user_ctx)
{
    BaseType_t higher_prio_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done_sem, &higher_prio_task_woken);
    return higher_prio_task_woken == pdTRUE;
}

static esp_err_t init_shared_spi_bus(void)
{
    ESP_LOGI(TAG, "Initializing shared SPI bus (SPI2_HOST) for LCD + SD card");

    spi_bus_config_t buscfg = {
        .sclk_io_num = SHARED_SPI_PIN_SCLK,
        .mosi_io_num = SHARED_SPI_PIN_MOSI,
        .miso_io_num = SHARED_SPI_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SHARED_SPI_MAX_TRANSFER_SZ,
    };

    /* This is called ONCE for the whole application. The SD card driver
     * (sd_storage.c) deliberately does NOT call spi_bus_initialize() again --
     * it attaches its own SPI device (different CS pin) to this same bus.
     * Calling spi_bus_initialize() twice for the same host is invalid and
     * will fail the second call. */
    esp_err_t ret = spi_bus_initialize(SHARED_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t init_backlight_pwm(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LCD_PIN_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ledc_timer_config_t ledc_timer = {
        .speed_mode = LCD_BL_LEDC_MODE,
        .timer_num = LCD_BL_LEDC_TIMER,
        .duty_resolution = LCD_BL_LEDC_DUTY_RES,
        .freq_hz = LCD_BL_LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LCD_BL_LEDC_MODE,
        .channel = LCD_BL_LEDC_CHANNEL,
        .timer_sel = LCD_BL_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = LCD_PIN_BACKLIGHT,
        .duty = 0,
        .hpoint = 0,
    };
    return ledc_channel_config(&ledc_channel);
}

void lcd_driver_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        ESP_LOGW(TAG, "Clamping brightness %u%% to 100%%", percent);
        percent = 100;
    }
    uint32_t max_duty = (1 << LCD_BL_LEDC_DUTY_RES) - 1;
    uint32_t duty = (percent * max_duty) / 100;
    ledc_set_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL, duty);
    ledc_update_duty(LCD_BL_LEDC_MODE, LCD_BL_LEDC_CHANNEL);
}

esp_err_t lcd_driver_init(void)
{
    esp_err_t ret;

    s_flush_done_sem = xSemaphoreCreateBinary();
    if (s_flush_done_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create flush-done semaphore");
        return ESP_ERR_NO_MEM;
    }
    /* Start "available" so the very first draw call doesn't block forever. */
    xSemaphoreGive(s_flush_done_sem);

    /* Serializes LCD draws against SD card reads on the shared SPI bus --
     * see spi_bus_lock.h for why this is needed. */
    shared_spi_lock_init();

    ret = init_shared_spi_bus();
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Installing LCD panel IO (SPI)");
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
    };
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SHARED_SPI_HOST, &io_config, &s_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
    };
    ESP_LOGI(TAG, "Installing ST7789 panel driver");
    ret = esp_lcd_new_panel_st7789(s_io_handle, &panel_config, &s_panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st7789 failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel_handle, LCD_MIRROR_X, LCD_MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel_handle, LCD_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, LCD_INVERT_COLOR));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    ret = init_backlight_pwm();
    if (ret != ESP_OK) {
        return ret;
    }
    lcd_driver_set_brightness(LCD_BL_DEFAULT_PERCENT);

    ESP_LOGI(TAG, "LCD ready: %dx%d, ST7789, RGB565", LCD_WIDTH, LCD_HEIGHT);
    return ESP_OK;
}

esp_err_t lcd_driver_draw_fullscreen(const uint16_t *rgb565_buffer)
{
    if (s_panel_handle == NULL || rgb565_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Hold the shared-bus lock for this entire call, including the wait for
     * DMA completion below. This guarantees an SD card read (reader_task,
     * on the other core) can never start a transaction on the shared SPI
     * hardware while this transfer is still in flight -- see
     * spi_bus_lock.h. This does mean this call now blocks until the pixel
     * data has actually finished transferring (no more free pipelining of
     * "decode next frame while this one is still going out"), which trades
     * a bit of throughput for not crashing; at 240x320/12fps there is still
     * comfortable headroom in the frame budget. */
    shared_spi_lock_acquire();

    /* Block until the PREVIOUS DMA transfer has completed. This is the
     * synchronization point that makes double buffering safe: by the time
     * this returns, it is safe for the caller to have started decoding into
     * this exact buffer (it is no longer being read by DMA). */
    xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);

    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_panel_handle, 0, 0,
                                               LCD_WIDTH, LCD_HEIGHT,
                                               rgb565_buffer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(ret));
        /* We consumed the semaphore but the transfer never started (and will
         * never call our completion callback), so give it back to avoid
         * permanently deadlocking the next draw call. */
        xSemaphoreGive(s_flush_done_sem);
        shared_spi_lock_release();
        return ret;
    }

    /* Wait for THIS transfer to finish before releasing the shared bus lock
     * -- this is the whole point: nothing else may touch the shared SPI
     * hardware until the DMA transfer it just started is actually done. */
    xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
    xSemaphoreGive(s_flush_done_sem);
    shared_spi_lock_release();
    return ESP_OK;
}

esp_err_t lcd_driver_fill(uint16_t rgb565_color)
{
    if (s_panel_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* One row at a time to avoid needing a full-frame scratch buffer just
     * to show a status color -- this path is not performance-critical. */
    uint16_t *row = heap_caps_malloc(LCD_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (row == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (int x = 0; x < LCD_WIDTH; x++) {
        row[x] = rgb565_color;
    }

    shared_spi_lock_acquire();
    esp_err_t ret = ESP_OK;
    for (int y = 0; y < LCD_HEIGHT && ret == ESP_OK; y++) {
        xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
        ret = esp_lcd_panel_draw_bitmap(s_panel_handle, 0, y, LCD_WIDTH, y + 1, row);
        if (ret != ESP_OK) {
            xSemaphoreGive(s_flush_done_sem);
        }
    }
    /* Wait for the last row's DMA to finish before freeing the buffer. */
    xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
    xSemaphoreGive(s_flush_done_sem);
    shared_spi_lock_release();

    heap_caps_free(row);
    return ret;
}

esp_err_t lcd_driver_draw_status_text(const char *text, uint16_t fg_color, uint16_t bg_color)
{
    if (s_panel_handle == NULL || text == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int text_len = (int)strlen(text);
    int scale = 3; /* upscale the tiny 5x7 font so it's readable at arm's length */
    int glyph_w = (SIMPLE_FONT_GLYPH_WIDTH + 1) * scale;
    int glyph_h = SIMPLE_FONT_GLYPH_HEIGHT * scale;
    int text_px_w = glyph_w * text_len;

    int x0 = (LCD_WIDTH - text_px_w) / 2;
    if (x0 < 0) {
        x0 = 0;
    }
    int y0 = (LCD_HEIGHT - glyph_h) / 2;

    /* Render into a small scratch buffer sized to the text's bounding box
     * (bounded by LCD_WIDTH), then push it in one shot. */
    int buf_w = LCD_WIDTH;
    uint16_t *line_buf = heap_caps_malloc(buf_w * glyph_h * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (line_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < buf_w * glyph_h; i++) {
        line_buf[i] = bg_color;
    }

    simple_font_draw_string(line_buf, buf_w, glyph_h, x0, 0, text, scale, fg_color);

    shared_spi_lock_acquire();
    xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
    esp_err_t ret = esp_lcd_panel_draw_bitmap(s_panel_handle, 0, y0, buf_w, y0 + glyph_h, line_buf);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_flush_done_sem);
    } else {
        /* Wait for completion before freeing the scratch buffer. */
        xSemaphoreTake(s_flush_done_sem, portMAX_DELAY);
        xSemaphoreGive(s_flush_done_sem);
    }
    shared_spi_lock_release();
    heap_caps_free(line_buf);
    return ret;
}
