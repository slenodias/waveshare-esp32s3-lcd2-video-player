#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "avi_reader.h"

static const char *TAG = "avi_reader";

#define FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

static const uint32_t FOURCC_RIFF = FOURCC('R', 'I', 'F', 'F');
static const uint32_t FOURCC_AVI  = FOURCC('A', 'V', 'I', ' ');
static const uint32_t FOURCC_LIST = FOURCC('L', 'I', 'S', 'T');
static const uint32_t FOURCC_HDRL = FOURCC('h', 'd', 'r', 'l');
static const uint32_t FOURCC_AVIH = FOURCC('a', 'v', 'i', 'h');
static const uint32_t FOURCC_MOVI = FOURCC('m', 'o', 'v', 'i');
static const uint32_t FOURCC_00DC = FOURCC('0', '0', 'd', 'c'); /* stream 0, compressed video */
static const uint32_t FOURCC_00DB = FOURCC('0', '0', 'd', 'b'); /* stream 0, uncompressed video (not used by us but recognized) */

/* Fields we actually need from AVIMAINHEADER (see Microsoft AVI RIFF spec).
 * The full struct is 56 bytes; we only read the leading fields we use and
 * skip the rest via the chunk's declared size, so this stays correct even
 * if an encoder pads the struct differently. */
#pragma pack(push, 1)
typedef struct {
    uint32_t dwMicroSecPerFrame;
    uint32_t dwMaxBytesPerSec;
    uint32_t dwPaddingGranularity;
    uint32_t dwFlags;
    uint32_t dwTotalFrames;
    uint32_t dwInitialFrames;
    uint32_t dwStreams;
    uint32_t dwSuggestedBufferSize;
    uint32_t dwWidth;
    uint32_t dwHeight;
} avi_main_header_t;
#pragma pack(pop)

static esp_err_t read_exact(FILE *fp, void *buf, size_t len)
{
    if (fread(buf, 1, len, fp) != len) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t read_chunk_header(FILE *fp, uint32_t *fourcc, uint32_t *size)
{
    uint8_t hdr[8];
    if (read_exact(fp, hdr, sizeof(hdr)) != ESP_OK) {
        return ESP_FAIL;
    }
    *fourcc = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    *size   = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    return ESP_OK;
}

static void skip_bytes(FILE *fp, long n)
{
    if (n > 0) {
        fseek(fp, n, SEEK_CUR);
    }
}

esp_err_t avi_open(const char *path, avi_context_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->fp = fopen(path, "rb");
    if (ctx->fp == NULL) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    /* --- Top-level RIFF header --- */
    uint32_t riff_fourcc, riff_size, riff_type;
    if (read_chunk_header(ctx->fp, &riff_fourcc, &riff_size) != ESP_OK ||
        riff_fourcc != FOURCC_RIFF) {
        ESP_LOGE(TAG, "Not a RIFF file (missing/garbled RIFF header)");
        goto fail;
    }
    if (read_exact(ctx->fp, &riff_type, 4) != ESP_OK || riff_type != FOURCC_AVI) {
        ESP_LOGE(TAG, "RIFF file is not type 'AVI ' -- unsupported container");
        goto fail;
    }

    bool found_avih = false;
    bool found_movi = false;

    /* Walk top-level chunks/lists inside the RIFF payload looking for
     * LIST 'hdrl' (to get avih) and LIST 'movi' (to get the frame data
     * range). We stop as soon as we have both. */
    while (!found_movi) {
        uint32_t fourcc, size;
        if (read_chunk_header(ctx->fp, &fourcc, &size) != ESP_OK) {
            break; /* EOF while scanning for movi -- handled below */
        }

        if (fourcc == FOURCC_LIST) {
            uint32_t list_type;
            if (read_exact(ctx->fp, &list_type, 4) != ESP_OK) {
                break;
            }
            long list_payload_remaining = (long)size - 4; /* size includes list_type */

            if (list_type == FOURCC_HDRL) {
                /* Scan inside hdrl for the avih chunk. */
                long hdrl_end = ftell(ctx->fp) + list_payload_remaining;
                while (ftell(ctx->fp) < hdrl_end) {
                    uint32_t sub_fourcc, sub_size;
                    if (read_chunk_header(ctx->fp, &sub_fourcc, &sub_size) != ESP_OK) {
                        break;
                    }
                    if (sub_fourcc == FOURCC_AVIH) {
                        avi_main_header_t mh = {0};
                        size_t to_read = sub_size < sizeof(mh) ? sub_size : sizeof(mh);
                        if (read_exact(ctx->fp, &mh, to_read) != ESP_OK) {
                            break;
                        }
                        /* Skip any remaining declared bytes of this chunk
                         * (struct may be slightly larger/smaller per-encoder). */
                        long remaining = (long)sub_size - (long)to_read;
                        skip_bytes(ctx->fp, remaining + (sub_size & 1));

                        ctx->width = mh.dwWidth;
                        ctx->height = mh.dwHeight;
                        ctx->total_frames = mh.dwTotalFrames;
                        ctx->frame_interval_ms = mh.dwMicroSecPerFrame / 1000;
                        found_avih = true;
                    } else {
                        skip_bytes(ctx->fp, sub_size + (sub_size & 1));
                    }
                }
                fseek(ctx->fp, hdrl_end + (hdrl_end & 1), SEEK_SET);
            } else if (list_type == FOURCC_MOVI) {
                /* Data starts right here; record the range and stop scanning. */
                ctx->movi_data_start = ftell(ctx->fp);
                ctx->movi_data_end = ctx->movi_data_start + list_payload_remaining;
                found_movi = true;
            } else {
                skip_bytes(ctx->fp, list_payload_remaining + (list_payload_remaining & 1));
            }
        } else {
            skip_bytes(ctx->fp, size + (size & 1));
        }
    }

    if (!found_movi) {
        ESP_LOGE(TAG, "Could not find 'movi' data list -- file is not a valid AVI, "
                      "or uses a structure this minimal parser does not support");
        goto fail;
    }
    if (!found_avih) {
        ESP_LOGW(TAG, "Could not find 'avih' header -- falling back to a default frame rate");
        ctx->frame_interval_ms = 100; /* 10 fps fallback, see video_player.h default */
    }
    if (ctx->frame_interval_ms == 0) {
        ESP_LOGW(TAG, "avih reported 0 us/frame -- falling back to a default frame rate");
        ctx->frame_interval_ms = 100;
    }

    ESP_LOGI(TAG, "AVI opened: %ux%u, ~%u ms/frame (%.1f fps), %u frames reported",
             (unsigned)ctx->width, (unsigned)ctx->height,
             (unsigned)ctx->frame_interval_ms, 1000.0 / ctx->frame_interval_ms,
             (unsigned)ctx->total_frames);

    return ESP_OK;

fail:
    fclose(ctx->fp);
    ctx->fp = NULL;
    return ESP_FAIL;
}

void avi_seek_to_start(avi_context_t *ctx)
{
    if (ctx->fp != NULL) {
        fseek(ctx->fp, ctx->movi_data_start, SEEK_SET);
    }
}

esp_err_t avi_read_next_frame(avi_context_t *ctx, uint8_t *buf, size_t buf_len, size_t *out_len)
{
    if (ctx->fp == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    while (ftell(ctx->fp) < ctx->movi_data_end) {
        uint32_t fourcc, size;
        if (read_chunk_header(ctx->fp, &fourcc, &size) != ESP_OK) {
            ESP_LOGE(TAG, "SD read error while reading a chunk header");
            return ESP_FAIL;
        }

        if (fourcc == FOURCC_00DC || fourcc == FOURCC_00DB) {
            if (size > buf_len) {
                ESP_LOGE(TAG, "Frame (%u bytes) exceeds buffer capacity (%u bytes) -- "
                              "increase VIDEO_MAX_JPEG_FRAME_SIZE in video_player.h",
                         (unsigned)size, (unsigned)buf_len);
                /* Skip the oversized frame's data so the stream stays in sync,
                 * then report the error to the caller for this frame only. */
                skip_bytes(ctx->fp, size + (size & 1));
                return ESP_ERR_INVALID_SIZE;
            }
            if (read_exact(ctx->fp, buf, size) != ESP_OK) {
                ESP_LOGE(TAG, "SD read error while reading frame data");
                return ESP_FAIL;
            }
            if (size & 1) {
                skip_bytes(ctx->fp, 1); /* RIFF chunks are word-aligned */
            }
            *out_len = size;
            return ESP_OK;
        }

        /* Not a video frame chunk (e.g. audio, or an index sub-chunk some
         * encoders embed inside movi) -- skip it and keep looking. */
        skip_bytes(ctx->fp, size + (size & 1));
    }

    return ESP_ERR_NOT_FOUND; /* end of movi list reached */
}

void avi_close(avi_context_t *ctx)
{
    if (ctx->fp != NULL) {
        fclose(ctx->fp);
        ctx->fp = NULL;
    }
}
