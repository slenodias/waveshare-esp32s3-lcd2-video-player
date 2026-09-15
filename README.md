# ESP32-S3 SD-Card Video Player — Waveshare ESP32-S3-LCD-2

A continuous-loop MJPEG video player for the Waveshare ESP32-S3-LCD-2.
Video is stored as an MJPEG/AVI file on a microSD card and streamed
frame-by-frame from the SD card to the onboard 240×320 ST7789 LCD, with no
audio support (this board has no audio hardware).

Built as a standalone ESP-IDF project — no LVGL, no third-party UI
framework, just the SD card, the JPEG decoder, and the display driver
talking directly to each other.

## Demo

<!--
  HOW TO ADD YOUR OUTPUT VIDEO:
  GitHub can't embed a video file just by linking to it in a repo the way
  images work -- .mp4/.mov files in a repo just show a "Download" link,
  not an inline player. To get an inline player like GitHub issues have:

  1. Open ANY issue, pull request, or a new Discussion on this repo
     (doesn't need to be submitted/saved).
  2. Drag your video file directly into the comment text box.
     GitHub uploads it and auto-inserts a link that looks like:
     https://github.com/user-attachments/assets/XXXXXXXX-XXXXXXXX
  3. Copy that URL (you can now close/discard the issue/comment --
     the uploaded file itself stays live).
  4. Replace the src="" below with that URL.

  Alternative: convert your clip to a short .gif and drop it directly in
  this repo (e.g. docs/demo.gif), then use:
  ![Demo](docs/demo.gif)
  GIFs DO render inline from files in the repo, unlike mp4.
-->

<video src="https://drive.google.com/file/d/1AjBHP7Yr1JcEDpecRlqBsMpb40xJTq4a/view?usp=sharing" controls width="400"></video>

*Video: MJPEG playback running on the Waveshare ESP32-S3-LCD-2.*

---

## How it works

The ESP32-S3 has no hardware video decoder (no H.264/H.265), so this
project doesn't attempt MP4 playback on-device. Instead, video is
pre-converted on a PC into **Motion-JPEG frames inside an AVI container**
— every frame is an independent JPEG image, decoded in software using
Espressif's SIMD-accelerated `esp_new_jpeg` component.

```
SD Card (microSD, SPI, shared bus with the LCD)
   |
   |  avi_reader.c: parses RIFF/AVI, walks the 'movi' chunk list,
   |  reads one "00dc" chunk (one JPEG frame) at a time
   v
reader_task (core 0)
   |  hands the compressed JPEG bytes to...
   v
FreeRTOS queue (3 buffers, pool-based, no heap churn)
   v
decoder_task (core 1)
   |  jpeg_decoder.c: esp_new_jpeg decodes JPEG -> RGB565 directly into
   |  one of two PSRAM framebuffers (double-buffered)
   v
lcd_driver.c: esp_lcd_panel_draw_bitmap() -> SPI + DMA -> ST7789 LCD
```

The reader and decoder run as two FreeRTOS tasks pinned to separate
cores so SD I/O never stalls JPEG decoding and vice versa. Memory usage
stays constant regardless of video length — nothing is buffered into
RAM beyond a small, fixed pool of frame buffers.

---

## Hardware

[Waveshare ESP32-S3-LCD-2](https://www.waveshare.com/esp32-s3-lcd-2.htm)

<img src="https://www.waveshare.com/media/catalog/product/cache/1/image/800x800/9df78eab33525d08d6e5fb8d27136e95/e/s/esp32-s3-lcd-2-1.jpg" alt="Waveshare ESP32-S3-LCD-2 board" width="400">

*Photo: [Waveshare](https://www.waveshare.com/esp32-s3-lcd-2.htm)*

The LCD and the microSD card share one SPI
bus (`SPI2_HOST`) — they use identical SCLK/MOSI/MISO pins and differ
only by CS pin. This is a genuine shared-bus design, not a wiring
shortcut.

| Function       | GPIO / Value       |
|-----------------|---------------------|
| SPI host (shared) | `SPI2_HOST` |
| SCLK / MOSI / MISO | GPIO 39 / 38 / 40 |
| LCD CS / DC / RST  | GPIO 45 / 42 / not connected |
| LCD backlight (PWM)| GPIO 1 (LEDC) |
| LCD controller     | ST7789, 240×320, RGB565 |
| SD CS               | GPIO 41 |
| SD filesystem        | FAT (via `esp_vfs_fat_sdspi_mount`) |
| PSRAM                 | Octal, 8 MB |
| ESP-IDF version        | v6.0.2 |

Full framebuffers and compressed-frame buffers are allocated from PSRAM
(`MALLOC_CAP_SPIRAM`) since the GDMA controller can DMA directly out of
PSRAM and internal SRAM is too small to hold buffers this size.

---

## Video format

| Setting     | Value | Why |
|-------------|-------|-----|
| Container   | AVI | matches what `avi_reader.c` parses |
| Codec       | MJPEG | only realistic option with no hardware video decode |
| Resolution  | 240 × 320 | matches the LCD exactly, no on-device scaling |
| Frame rate  | 10–12 fps | sustainable given SPI transfer + decode + SD read budget |
| Quality     | `-q:v 3` (ffmpeg 2–31 scale, lower = better) | `-q:v 6` was tried first and produced visible block artifacts on gradients/detail at this resolution; `-q:v 3` fixed it |
| Audio       | None | not implemented, board has no audio output |

### Converting a video

```bash
ffmpeg -i input.mp4 \
  -vf "scale=240:320:force_original_aspect_ratio=decrease,pad=240:320:(ow-iw)/2:(oh-ih)/2,fps=12" \
  -c:v mjpeg -q:v 3 -an \
  video.avi
```

- `scale`/`pad` letterboxes the source into exactly 240×320 without
  distortion, regardless of the input's aspect ratio.
- `fps=12` resamples to the target frame rate.
- `-an` strips audio.
- Copy the resulting `video.avi` to the root of the SD card. `VIDEO_FILE_PATH`
  in `board_config.h` defaults to `/sdcard/video.avi`.
- **Sanity-check the file in VLC (or any PC video player) before
  copying it to the SD card.** If it already looks wrong on the PC, the
  problem is the encode, not the firmware — cheaper to catch there.
- If any frame comes out larger than `VIDEO_MAX_JPEG_FRAME_SIZE` (96 KB,
  in `video_player.h`), raise `-q:v` (worse quality) or raise that
  constant.

---

## Setup

1. Copy this project folder somewhere on disk, e.g. `~/esp/video_player`.
2. Open it in VS Code with the Espressif IDF extension (or use `idf.py`
   directly from a terminal with `IDF_PATH` set to your ESP-IDF v6.0.2
   install).
3. `idf.py set-target esp32s3`
4. `idf.py build` — the first build fetches `espressif/esp_new_jpeg` via
   the IDF Component Manager automatically (needs internet access once).
5. `idf.py -p <PORT> flash monitor`
6. Format the SD card FAT32, convert a video per the section above, and
   copy `video.avi` to its root.
7. Insert the card and power/reset the board. Playback starts
   automatically and loops continuously.

---

## Known issues fixed during bring-up

These were hit and resolved while getting the project running on real
hardware — noted here in case they resurface after an ESP-IDF upgrade
or a hardware revision change.

**Build: `fatal error: driver/ledc.h: No such file or directory`**
ESP-IDF v6.0.2 split the old monolithic `driver` component into
per-peripheral components (`esp_driver_gpio`, `esp_driver_spi`, etc.).
LEDC now lives in its own component, `esp_driver_ledc`, which isn't
pulled in automatically by requiring `driver`. Fix: add
`esp_driver_ledc` to `PRIV_REQUIRES` in `main/CMakeLists.txt`.

**Runtime: boot-loop, `assert failed: spi_hal_setup_trans ...
spi_ll_get_running_cmd(hw) == 0`, crash during playback**
The LCD and SD card share one SPI bus. The LCD draw
(`esp_lcd_panel_draw_bitmap`) is asynchronous — it starts a DMA
transfer and returns before it finishes — while the SD card read
happens on a different FreeRTOS task pinned to the other core. Nothing
stopped the two from touching the shared SPI hardware at the same
time, which the low-level driver doesn't tolerate. Fix: added
`spi_bus_lock.h/.c`, a small mutex that both the LCD draw path
(`lcd_driver.c`) and the SD read path (`reader_task` in
`video_player.c`) now hold for the *entire* duration of their SPI
activity, including waiting for DMA completion — guaranteeing the two
peripherals never touch the shared bus concurrently.

**Runtime: image displays with heavy blocking artifacts and wrong colors
(orange background rendering as blue/purple, detail areas turning to
color noise)**
Two separate causes, fixed independently:
- Overly aggressive JPEG compression (`-q:v 6` was too low-quality for
  this content at 240×320 — average frame size was ~4.2 KB, well under
  what's needed for clean output). Fixed by re-encoding at `-q:v 3`.
- RGB565 byte order mismatch between the JPEG decoder output and what
  the ST7789 panel expects over SPI. `jpeg_decoder.c` was configured
  for `JPEG_PIXEL_FORMAT_RGB565_LE`; switching to
  `JPEG_PIXEL_FORMAT_RGB565_BE` fixed the color mapping.

---

## Project files

```
video_player/
├── CMakeLists.txt
├── sdkconfig.defaults
├── README.md
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml
    ├── board_config.h       - all pins/timings
    ├── main.c                - boot sequence, error screens
    ├── lcd_driver.h/.c       - ST7789 init + full-screen DMA draw
    ├── sd_storage.h/.c       - shared-bus SD/FAT mount
    ├── avi_reader.h/.c       - minimal streaming AVI/MJPEG demuxer
    ├── jpeg_decoder.h/.c     - wraps espressif/esp_new_jpeg
    ├── spi_bus_lock.h/.c     - serializes LCD and SD access on the shared SPI bus
    ├── simple_font.h/.c      - tiny built-in font for on-screen error text
    └── video_player.h/.c     - reader/decoder tasks, buffer pool, recovery
```

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| SD card not detected | Check wiring (MOSI=38, MISO=40, CLK=39, CS=41), pull-ups on SD lines, try a different card |
| LCD stays white/black at boot | Check monitor log — LCD init failed before it could show an error on-screen. Re-check LCD pins and backlight wiring |
| `FILE NOT FOUND` on screen | Confirm the file is at `/video.avi` on the card's root and the card mounted successfully |
| Boot-loop with SPI HAL assert | Should be fixed by the shared-bus lock above — if it reappears, confirm `spi_bus_lock.c` is in the build and every SD/LCD SPI call goes through it |
| Wrong colors / block artifacts | See "Known issues" above — check `jpeg_decoder.c`'s output pixel format and the ffmpeg `-q:v` value used |
| Playback slow / uneven frame rate | Lower fps or raise `-q:v` in the ffmpeg command, or use a faster SD card |
| `MEMORY ERROR` at boot | Confirm PSRAM is detected in the boot log; check `sdkconfig.defaults`' `CONFIG_SPIRAM*` settings survived any `menuconfig` changes |
