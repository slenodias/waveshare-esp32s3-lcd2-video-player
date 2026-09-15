/**
 * video_player.h
 *
 * Ties together avi_reader (SD) -> jpeg_decoder (CPU) -> lcd_driver (DMA)
 * using two FreeRTOS tasks and a small fixed pool of buffers, so memory
 * usage stays constant regardless of video length:
 *
 *   SD Card --read--> [reader_task] --queue--> [decoder_task] --DMA--> LCD
 *                          ^                         |
 *                          '------ free_queue <------'
 *
 * reader_task (pinned to core 0): pulls an empty compressed-frame buffer
 * from free_queue, fills it from the AVI file on the SD card, and pushes
 * it to filled_queue. On end-of-file it seeks back to the start (loop).
 *
 * decoder_task (pinned to core 1): pulls a compressed frame from
 * filled_queue, JPEG-decodes it into one of two RGB565 framebuffers, and
 * hands that to the LCD driver, then returns the compressed buffer to
 * free_queue. Framebuffers are double-buffered so decoding the next frame
 * can overlap with the DMA transfer of the previous one.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upper bound on one compressed MJPEG frame's size. If your encoded video
 * needs bigger frames than this (very high quality / high resolution),
 * raise this value -- see the "insufficient memory" entry in the
 * README troubleshooting section. */
#define VIDEO_MAX_JPEG_FRAME_SIZE   (96 * 1024)

/* Number of compressed-frame buffers in flight between the reader and
 * decoder tasks. 3 gives the reader room to stay ahead of the decoder
 * without unbounded memory growth. */
#define VIDEO_NUM_JPEG_BUFFERS      3

/** Fallback frame rate used only if the AVI's own header can't be parsed. */
#define VIDEO_DEFAULT_FRAME_INTERVAL_MS  100 /* 10 fps */

/**
 * Starts video playback in background tasks. Assumes lcd_driver_init()
 * and sd_storage_init() have already succeeded and the file at `path`
 * exists (caller should have checked via sd_storage_check_video_file()).
 *
 * This function itself does the (potentially slow) work of opening the
 * AVI file and allocating buffers synchronously, and returns ESP_OK once
 * playback tasks are running, or an error if setup failed.
 */
esp_err_t video_player_start(const char *path);

#ifdef __cplusplus
}
#endif
