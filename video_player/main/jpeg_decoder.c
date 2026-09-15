#include <string.h>

#include "esp_log.h"
#include "esp_jpeg_dec.h"   /* provided by the espressif/esp_new_jpeg component */

#include "jpeg_decoder.h"

static const char *TAG = "jpeg_decoder";

static int s_expected_width = 0;
static int s_expected_height = 0;

esp_err_t jpeg_decoder_init(int expected_width, int expected_height)
{
    s_expected_width = expected_width;
    s_expected_height = expected_height;
    ESP_LOGI(TAG, "JPEG decoder ready for %dx%d RGB565 frames", expected_width, expected_height);
    return ESP_OK;
}

esp_err_t jpeg_decoder_decode_frame(const uint8_t *jpeg_data, size_t jpeg_len, uint16_t *out_rgb565)
{
    if (jpeg_data == NULL || out_rgb565 == NULL || jpeg_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* We open/close the decoder handle per frame. This matches Espressif's
     * documented single-image decode sequence exactly (open -> parse_header
     * -> process -> close) and avoids any assumption about whether internal
     * state safely resets between images on a long-lived handle. The
     * open/close calls themselves do not allocate the (large) pixel
     * buffers, so the per-frame cost is small relative to the decode itself. */
    jpeg_dec_config_t config = {
        /* Was RGB565_LE. The ST7789 panel over esp_lcd_panel_io_spi expects
         * each 16-bit pixel big-endian (high byte first) on the wire; if the
         * decoder was handing us little-endian pixels, every pixel's bytes
         * were effectively swapped, which reshuffles which bits land in the
         * R/G/B fields -- producing a consistent but wrong color mapping
         * (e.g. orange rendering as blue/purple) rather than real noise. */
        .output_type = JPEG_PIXEL_FORMAT_RGB565_BE,
        .rotate = JPEG_ROTATE_0D,
    };

    jpeg_dec_handle_t dec = NULL;
    jpeg_error_t jret = jpeg_dec_open(&config, &dec);
    if (jret != JPEG_ERR_OK || dec == NULL) {
        ESP_LOGE(TAG, "jpeg_dec_open failed (%d)", (int)jret);
        return ESP_FAIL;
    }

    jpeg_dec_io_t io = {0};
    jpeg_dec_header_info_t header_info = {0};

    io.inbuf = (uint8_t *)jpeg_data;
    io.inbuf_len = (int)jpeg_len;

    jret = jpeg_dec_parse_header(dec, &io, &header_info);
    if (jret != JPEG_ERR_OK) {
        ESP_LOGW(TAG, "jpeg_dec_parse_header failed (%d) -- corrupt/truncated frame, skipping", (int)jret);
        jpeg_dec_close(dec);
        return ESP_FAIL;
    }

    if (header_info.width != s_expected_width || header_info.height != s_expected_height) {
        ESP_LOGE(TAG, "Frame is %dx%d but decoder was set up for %dx%d -- "
                      "check that your ffmpeg output resolution matches the LCD",
                 (int)header_info.width, (int)header_info.height,
                 s_expected_width, s_expected_height);
        jpeg_dec_close(dec);
        return ESP_ERR_INVALID_SIZE;
    }

    io.outbuf = (uint8_t *)out_rgb565;

    jret = jpeg_dec_process(dec, &io);
    jpeg_dec_close(dec);

    if (jret != JPEG_ERR_OK) {
        ESP_LOGW(TAG, "jpeg_dec_process failed (%d) -- skipping this frame", (int)jret);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void jpeg_decoder_deinit(void)
{
    /* No persistent handle is kept between frames -- nothing to release. */
}
