/**
 * jpeg_decoder.h
 *
 * Thin wrapper around Espressif's `esp_new_jpeg` component
 * (idf.py add-dependency "espressif/esp_new_jpeg", declared in
 * main/idf_component.yml) to decode one MJPEG frame straight into an
 * RGB565 buffer sized for the LCD.
 *
 * WHY THIS COMPONENT AND NOT H.264:
 * The ESP32-S3 has no hardware video decoder of any kind (H.264 or
 * otherwise) -- see the "Recommended video format" section of the README
 * for the full explanation. esp_new_jpeg is a software JPEG codec that
 * Espressif hand-optimized with the ESP32-S3's Xtensa SIMD (PIE)
 * instructions, which is why MJPEG (one independent JPEG frame at a time,
 * no inter-frame prediction) is the practical choice here.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One-time init of the decoder wrapper (allocates internal handles). */
esp_err_t jpeg_decoder_init(int expected_width, int expected_height);

/**
 * Decodes one JPEG frame (`jpeg_len` bytes at `jpeg_data`) into
 * `out_rgb565`, which must be at least expected_width * expected_height *
 * 2 bytes (as passed to jpeg_decoder_init). The output is little-endian
 * RGB565, matching what esp_lcd_panel_draw_bitmap() expects for this
 * panel.
 *
 * Returns ESP_OK on success, ESP_ERR_INVALID_SIZE if the JPEG's own
 * dimensions don't match what the decoder was configured for, or
 * ESP_FAIL for a corrupt/undecodable frame (caller should just skip the
 * frame and keep playing -- do not treat this as fatal).
 */
esp_err_t jpeg_decoder_decode_frame(const uint8_t *jpeg_data, size_t jpeg_len,
                                     uint16_t *out_rgb565);

void jpeg_decoder_deinit(void);

#ifdef __cplusplus
}
#endif
