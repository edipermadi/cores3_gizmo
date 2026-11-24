/*
 * Simulated touch input:
 *
 *   touch X Y [MS]               tap at X,Y held for MS ms (default 100)
 *   swipe X1 Y1 X2 Y2 [MS]       press at X1,Y1, move to X2,Y2 over MS ms
 *                                (default 300), release
 *
 * A second LVGL pointer input device replays one "gesture": while pressed it
 * reports a position interpolated linearly from start to end over the
 * gesture's duration. LVGL treats it like the real touch panel, so taps,
 * long-presses, slider drags, scrolling and swipes all work. A tap is simply
 * a gesture whose start and end are the same point.
 *
 * LVGL samples input every indev read period (~30 ms), so very short swipes
 * deliver few points; 200-500 ms gives smooth drags.
 */
#include <stdlib.h>

#include "argtable3/argtable3.h"
#include "console_priv.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "gizmo_core.h"
#include "lvgl.h"

#define TOUCH_DEFAULT_MS  100
#define SWIPE_DEFAULT_MS  300
#define MAX_MS            10000

/* The gesture being replayed; guarded by s_lock (read from the LVGL task). */
static struct {
    int32_t x0, y0, x1, y1;
    int64_t start_us;
    int64_t dur_us;
} s_g;

static lv_indev_t *s_indev;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int64_t now = esp_timer_get_time();

    taskENTER_CRITICAL(&s_lock);
    int64_t t = now - s_g.start_us;
    bool pressed = s_g.dur_us > 0 && t >= 0 && t < s_g.dur_us;
    if (t < 0) {
        t = 0;
    }
    if (t > s_g.dur_us) {
        t = s_g.dur_us;   /* after release, stay at the end point */
    }
    int32_t x = s_g.x0, y = s_g.y0;
    if (s_g.dur_us > 0) {
        x += (int32_t)((s_g.x1 - s_g.x0) * t / s_g.dur_us);
        y += (int32_t)((s_g.y1 - s_g.y0) * t / s_g.dur_us);
    }
    taskEXIT_CRITICAL(&s_lock);

    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static bool in_screen(int x, int y)
{
    return x >= 0 && y >= 0 &&
           x < lv_display_get_horizontal_resolution(NULL) &&
           y < lv_display_get_vertical_resolution(NULL);
}

/** Start a gesture and block until it has been released. */
static void play(int x0, int y0, int x1, int y1, int ms)
{
    taskENTER_CRITICAL(&s_lock);
    s_g.x0 = x0;
    s_g.y0 = y0;
    s_g.x1 = x1;
    s_g.y1 = y1;
    s_g.dur_us = (int64_t)ms * 1000;
    s_g.start_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&s_lock);

    /* Wait past the release (plus a read period) so scripted gestures
     * don't overlap. */
    vTaskDelay(pdMS_TO_TICKS(ms + 60));
}

static void print_range_help(void)
{
    printf("out of range: 0<=x<%ld 0<=y<%ld 1<=ms<=%d\n",
           (long)lv_display_get_horizontal_resolution(NULL),
           (long)lv_display_get_vertical_resolution(NULL), MAX_MS);
}

/* ---------- touch ---------- */

static struct {
    struct arg_int *x;
    struct arg_int *y;
    struct arg_int *ms;
    struct arg_end *end;
} s_touch_args;

static int touch_cmd(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **)&s_touch_args) != 0) {
        arg_print_errors(stderr, s_touch_args.end, argv[0]);
        return 1;
    }
    int x = s_touch_args.x->ival[0], y = s_touch_args.y->ival[0];
    int ms = s_touch_args.ms->count ? s_touch_args.ms->ival[0] : TOUCH_DEFAULT_MS;
    if (!in_screen(x, y) || ms < 1 || ms > MAX_MS) {
        print_range_help();
        return 1;
    }
    play(x, y, x, y, ms);
    printf("touched %d,%d for %d ms\n", x, y, ms);
    return 0;
}

/* ---------- swipe ---------- */

static struct {
    struct arg_int *x1;
    struct arg_int *y1;
    struct arg_int *x2;
    struct arg_int *y2;
    struct arg_int *ms;
    struct arg_end *end;
} s_swipe_args;

static int swipe_cmd(int argc, char **argv)
{
    if (arg_parse(argc, argv, (void **)&s_swipe_args) != 0) {
        arg_print_errors(stderr, s_swipe_args.end, argv[0]);
        return 1;
    }
    int x1 = s_swipe_args.x1->ival[0], y1 = s_swipe_args.y1->ival[0];
    int x2 = s_swipe_args.x2->ival[0], y2 = s_swipe_args.y2->ival[0];
    int ms = s_swipe_args.ms->count ? s_swipe_args.ms->ival[0] : SWIPE_DEFAULT_MS;
    if (!in_screen(x1, y1) || !in_screen(x2, y2) || ms < 1 || ms > MAX_MS) {
        print_range_help();
        return 1;
    }
    play(x1, y1, x2, y2, ms);
    printf("swiped %d,%d -> %d,%d in %d ms\n", x1, y1, x2, y2, ms);
    return 0;
}

/* ---------- registration ---------- */

esp_err_t cmd_touch_register(void)
{
    gizmo_core_lock(0);
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, read_cb);
    gizmo_core_unlock();

    s_touch_args.x = arg_int1(NULL, NULL, "<x>", "X coordinate (0..319)");
    s_touch_args.y = arg_int1(NULL, NULL, "<y>", "Y coordinate (0..239)");
    s_touch_args.ms = arg_int0(NULL, NULL, "<ms>", "press duration, default 100");
    s_touch_args.end = arg_end(3);
    const esp_console_cmd_t touch = {
        .command = "touch",
        .help = "Simulate a tap at X,Y (optionally held for MS ms)",
        .func = touch_cmd,
        .argtable = &s_touch_args,
    };
    esp_err_t err = esp_console_cmd_register(&touch);
    if (err != ESP_OK) {
        return err;
    }

    s_swipe_args.x1 = arg_int1(NULL, NULL, "<x1>", "start X");
    s_swipe_args.y1 = arg_int1(NULL, NULL, "<y1>", "start Y");
    s_swipe_args.x2 = arg_int1(NULL, NULL, "<x2>", "end X");
    s_swipe_args.y2 = arg_int1(NULL, NULL, "<y2>", "end Y");
    s_swipe_args.ms = arg_int0(NULL, NULL, "<ms>", "duration, default 300");
    s_swipe_args.end = arg_end(5);
    const esp_console_cmd_t swipe = {
        .command = "swipe",
        .help = "Simulate a drag from X1,Y1 to X2,Y2 over MS ms",
        .func = swipe_cmd,
        .argtable = &s_swipe_args,
    };
    return esp_console_cmd_register(&swipe);
}
