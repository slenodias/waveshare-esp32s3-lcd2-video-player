/**
 * avi_reader.h
 *
 * Minimal streaming reader for AVI files containing an MJPEG video stream
 * (the format ffmpeg produces with `-c:v mjpeg ... output.avi`, no audio).
 *
 * This is NOT a general-purpose AVI demuxer -- it deliberately supports
 * only what a single-video-stream, MJPEG, ffmpeg-generated AVI needs:
 *   RIFF ---- AVI
 *     LIST -- hdrl
 *       avih  (main header: frame rate, dimensions, frame count)
 *       LIST - strl  (skipped in detail; we don't need per-stream codec
 *                      negotiation because we already know it's MJPEG)
 *     LIST -- movi
 *       00dc <jpeg bytes>   <- one chunk per video frame
 *       00dc <jpeg bytes>
 *       ...
 *
 * Frames are read one at a time directly from the SD card (no whole-file
 * buffering), which is what keeps RAM usage bounded regardless of video
 * length.
 */

#pragma once

#include <stdio.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    FILE *fp;
    long movi_data_start;   /* file offset of the first frame chunk header */
    long movi_data_end;     /* file offset just past the last byte of the movi list */
    uint32_t width;
    uint32_t height;
    uint32_t frame_interval_ms; /* derived from avih's dwMicroSecPerFrame */
    uint32_t total_frames;      /* informational only, from avih */
} avi_context_t;

/**
 * Opens `path`, validates the RIFF/AVI/movi structure, and parses the
 * avih header for timing/dimension metadata. Does not read any frame
 * data yet.
 */
esp_err_t avi_open(const char *path, avi_context_t *ctx);

/** Seeks back to the first frame chunk -- used to loop playback. */
void avi_seek_to_start(avi_context_t *ctx);

/**
 * Reads the next MJPEG video frame's compressed bytes into `buf`
 * (capacity `buf_len`). Non-video chunks inside `movi` (e.g. an audio
 * stream, or index sub-chunks some encoders interleave) are skipped
 * transparently.
 *
 * Returns:
 *   ESP_OK              - a frame was read, *out_len holds its size
 *   ESP_ERR_NOT_FOUND    - reached the end of the movi list (EOF) -- caller
 *                          should call avi_seek_to_start() to loop
 *   ESP_ERR_INVALID_SIZE - a chunk was larger than buf_len (caller's frame
 *                          buffer is too small for this file / bitrate)
 *   ESP_FAIL             - SD read error
 */
esp_err_t avi_read_next_frame(avi_context_t *ctx, uint8_t *buf, size_t buf_len, size_t *out_len);

void avi_close(avi_context_t *ctx);

#ifdef __cplusplus
}
#endif
