/*
 * `screenshot`: active screen -> RGB565 -> zlib (ROM miniz) -> base64.
 *
 * Output (one line each, base64 wrapped at 76 chars):
 *
 *   SCREENSHOT BEGIN w=320 h=240 fmt=rgb565le raw=153600 zlen=12345
 *   eNrt3U...
 *   SCREENSHOT END
 *
 * Log lines from other tasks may appear between the markers; a decoder should
 * keep only lines made of base64 characters (tools/gizmo.py does).
 *
 * The snapshot renders into a PSRAM buffer we own (LVGL's heap is small), and
 * only the snapshot itself holds the LVGL lock; compression runs unlocked.
 * Layers above the screen (e.g. the Wi-Fi password dialog) are not included.
 */
#include <stdio.h>
#include <string.h>

#include "console_priv.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "gizmo_core.h"
#include "lvgl.h"
#include "miniz.h"

#define B64_LINE_BYTES 57   /* -> 76 base64 chars per line */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void print_base64(const uint8_t *data, size_t len)
{
    char line[80];
    for (size_t off = 0; off < len; off += B64_LINE_BYTES) {
        size_t n = len - off < B64_LINE_BYTES ? len - off : B64_LINE_BYTES;
        char *p = line;
        for (size_t i = 0; i < n; i += 3) {
            uint32_t v = (uint32_t)data[off + i] << 16;
            if (i + 1 < n) v |= (uint32_t)data[off + i + 1] << 8;
            if (i + 2 < n) v |= data[off + i + 2];
            *p++ = B64[(v >> 18) & 63];
            *p++ = B64[(v >> 12) & 63];
            *p++ = i + 1 < n ? B64[(v >> 6) & 63] : '=';
            *p++ = i + 2 < n ? B64[v & 63] : '=';
        }
        *p = '\0';
        puts(line);
    }
}

static int screenshot_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    const uint32_t w = lv_display_get_horizontal_resolution(NULL);
    const uint32_t h = lv_display_get_vertical_resolution(NULL);
    const uint32_t stride = w * 2;
    const size_t raw_len = (size_t)stride * h;
    const size_t out_cap = raw_len + raw_len / 1000 + 1024;   /* deflate worst case */

    uint8_t *pixels = heap_caps_malloc(raw_len, MALLOC_CAP_SPIRAM);
    uint8_t *out = heap_caps_malloc(out_cap, MALLOC_CAP_SPIRAM);
    tdefl_compressor *comp = heap_caps_malloc(sizeof(*comp), MALLOC_CAP_SPIRAM);
    int rc = 1;
    if (!pixels || !out || !comp) {
        printf("screenshot: out of memory\n");
        goto done;
    }

    int64_t t0 = esp_timer_get_time();

    lv_draw_buf_t buf;
    gizmo_core_lock(0);
    lv_result_t res = lv_draw_buf_init(&buf, w, h, LV_COLOR_FORMAT_RGB565, stride, pixels, raw_len);
    if (res == LV_RESULT_OK) {
        res = lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565, &buf);
    }
    gizmo_core_unlock();
    if (res != LV_RESULT_OK) {
        printf("screenshot: snapshot failed\n");
        goto done;
    }

    int64_t t1 = esp_timer_get_time();

    size_t in_len = raw_len, out_len = out_cap;
    tdefl_init(comp, NULL, NULL, TDEFL_WRITE_ZLIB_HEADER | TDEFL_DEFAULT_MAX_PROBES);
    tdefl_status st = tdefl_compress(comp, pixels, &in_len, out, &out_len, TDEFL_FINISH);
    if (st != TDEFL_STATUS_DONE) {
        printf("screenshot: compression failed (%d)\n", (int)st);
        goto done;
    }

    int64_t t2 = esp_timer_get_time();

    printf("SCREENSHOT BEGIN w=%lu h=%lu fmt=rgb565le raw=%u zlen=%u\n",
           (unsigned long)w, (unsigned long)h, (unsigned)raw_len, (unsigned)out_len);
    print_base64(out, out_len);
    printf("SCREENSHOT END\n");
    printf("snapshot %lld ms, compress %lld ms, %u -> %u bytes (%.1f%%)\n",
           (t1 - t0) / 1000, (t2 - t1) / 1000, (unsigned)raw_len, (unsigned)out_len,
           100.0 * out_len / raw_len);
    fflush(stdout);
    rc = 0;

done:
    heap_caps_free(comp);
    heap_caps_free(out);
    heap_caps_free(pixels);
    return rc;
}

esp_err_t cmd_screenshot_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "screenshot",
        .help = "Dump the active screen (RGB565, zlib, base64). Decode with tools/gizmo.py",
        .func = screenshot_cmd,
    };
    return esp_console_cmd_register(&cmd);
}
