/*
 * "Spectrum" screen: live microphone spectrum, two views. Tap the plot or
 * push the encoder to switch between them.
 *
 *   Bars       32 log-spaced bars with falling peak markers.
 *   Waterfall  spectrogram: newest spectrum on the top row, history scrolling
 *              down; frequency left (60 Hz) to right (8 kHz), colour = level.
 *
 *   [<] Spectrum                      [status]
 *   Peak 1.25 kHz  -34 dB        In -18 dBFS
 *   [ plot area 304 x 140 ]
 *   100        1k            4k      8k
 *
 * The analyser (gizmo_core) runs only while this screen is shown. In bars
 * mode bars rise instantly and fall at BAR_FALL per frame; peak markers hold
 * PEAK_HOLD frames then fall at PEAK_FALL per frame. The waterfall adds one
 * row per new analyser frame (~31 rows/s, so ~4.5 s of history).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ui_priv.h"

static const char *TAG = "ui_spectrum";

#define FRAME_MS    33
#define AREA_X      8
#define AREA_W      304
#define AREA_Y      (UI_HEADER_H + 30)
#define AREA_H      140
#define BAR_GAP     2
#define BAR_FALL    0.035f
#define PEAK_FALL   0.008f
#define PEAK_HOLD   20

#define F_MIN       60.0f
#define F_MAX       8000.0f

typedef enum { MODE_BARS, MODE_WATERFALL } view_mode_t;

static lv_obj_t *s_scr;
static lv_group_t *s_group;
static lv_obj_t *s_info;
static lv_obj_t *s_input;
static lv_timer_t *s_timer;
static bool s_running;
static view_mode_t s_mode = MODE_BARS;
static lv_obj_t *s_toggle;          /* transparent button over the plot */

/* Bars view */
static lv_obj_t *s_bars_box;
static lv_obj_t *s_bars[GIZMO_SPECTRUM_BANDS];
static lv_obj_t *s_peaks[GIZMO_SPECTRUM_BANDS];
static float s_disp[GIZMO_SPECTRUM_BANDS];
static float s_peak[GIZMO_SPECTRUM_BANDS];
static int s_hold[GIZMO_SPECTRUM_BANDS];

/* Waterfall view */
static lv_obj_t *s_wf;
static uint16_t *s_wf_buf;          /* AREA_W x AREA_H RGB565, PSRAM */
static uint16_t s_lut[256];         /* level 0..255 -> RGB565 */
static uint32_t s_last_seq;

/* ---------- helpers ---------- */

static int32_t bar_x(int i)
{
    return AREA_X + (int32_t)i * AREA_W / GIZMO_SPECTRUM_BANDS;
}

static int32_t bar_w(void)
{
    return AREA_W / GIZMO_SPECTRUM_BANDS - BAR_GAP;
}

/** Black -> blue -> purple -> red -> yellow -> white. */
static void build_lut(void)
{
    static const struct { float pos; uint8_t r, g, b; } stops[] = {
        { 0.00f,   0,   0,   0 },
        { 0.20f,  10,  20, 120 },
        { 0.40f, 120,  20, 160 },
        { 0.60f, 230,  40,  60 },
        { 0.80f, 255, 190,  20 },
        { 1.00f, 255, 255, 255 },
    };
    const int n = sizeof(stops) / sizeof(stops[0]);
    for (int i = 0; i < 256; i++) {
        float v = i / 255.0f;
        int s = 0;
        while (s < n - 2 && v > stops[s + 1].pos) {
            s++;
        }
        float t = (v - stops[s].pos) / (stops[s + 1].pos - stops[s].pos);
        uint8_t r = (uint8_t)(stops[s].r + t * (stops[s + 1].r - stops[s].r));
        uint8_t g = (uint8_t)(stops[s].g + t * (stops[s + 1].g - stops[s].g));
        uint8_t b = (uint8_t)(stops[s].b + t * (stops[s + 1].b - stops[s].b));
        s_lut[i] = lv_color_to_u16(lv_color_make(r, g, b));
    }
}

static void waterfall_clear(void)
{
    if (s_wf_buf) {
        memset(s_wf_buf, 0, (size_t)AREA_W * AREA_H * sizeof(uint16_t));
        lv_obj_invalidate(s_wf);
    }
}

/** Shift history down one row and paint the newest spectrum on top. */
static void waterfall_push(const gizmo_spectrum_t *sp)
{
    memmove(s_wf_buf + AREA_W, s_wf_buf, (size_t)AREA_W * (AREA_H - 1) * sizeof(uint16_t));
    for (int x = 0; x < AREA_W; x++) {
        s_wf_buf[x] = s_lut[sp->fine[x * GIZMO_SPECTRUM_FINE / AREA_W]];
    }
    lv_obj_invalidate(s_wf);
}

static void bars_update(const gizmo_spectrum_t *sp)
{
    for (int i = 0; i < GIZMO_SPECTRUM_BANDS; i++) {
        float v = sp->level[i];
        s_disp[i] = v > s_disp[i] ? v : fmaxf(v, s_disp[i] - BAR_FALL);

        if (s_disp[i] >= s_peak[i]) {
            s_peak[i] = s_disp[i];
            s_hold[i] = PEAK_HOLD;
        } else if (s_hold[i] > 0) {
            s_hold[i]--;
        } else {
            s_peak[i] = fmaxf(s_disp[i], s_peak[i] - PEAK_FALL);
        }

        int32_t h = (int32_t)(s_disp[i] * AREA_H);
        if (h < 1) {
            h = 1;
        }
        lv_obj_set_height(s_bars[i], h);
        lv_obj_set_y(s_bars[i], AREA_Y + AREA_H - h);
        lv_obj_set_y(s_peaks[i], AREA_Y + AREA_H - (int32_t)(s_peak[i] * AREA_H) - 2);
    }
}

static void info_update(const gizmo_spectrum_t *sp)
{
    if (sp->peak_db > -75.0f) {
        if (sp->peak_hz >= 1000.0f) {
            lv_label_set_text_fmt(s_info, "Peak %d.%02d kHz  %d dB", (int)(sp->peak_hz / 1000),
                                  ((int)sp->peak_hz % 1000) / 10, (int)sp->peak_db);
        } else {
            lv_label_set_text_fmt(s_info, "Peak %d Hz  %d dB", (int)sp->peak_hz, (int)sp->peak_db);
        }
    } else {
        lv_label_set_text(s_info, "Quiet");
    }
    lv_label_set_text_fmt(s_input, "In %d dBFS", (int)sp->input_dbfs);
    lv_obj_set_style_text_color(s_input, sp->input_dbfs > -3.0f ? UI_COLOR_LOW : UI_COLOR_SUBTEXT, 0);
}

static void frame_cb(lv_timer_t *t)
{
    (void)t;
    gizmo_spectrum_t sp;
    if (gizmo_core_spectrum_get(&sp) != ESP_OK) {
        return;
    }

    if (s_mode == MODE_BARS) {
        bars_update(&sp);
    } else if (s_wf_buf && sp.seq != s_last_seq) {
        waterfall_push(&sp);
    }
    s_last_seq = sp.seq;
    info_update(&sp);
}

static void set_mode(view_mode_t mode)
{
    s_mode = mode;
    if (mode == MODE_BARS) {
        lv_obj_add_flag(s_wf, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_bars_box, LV_OBJ_FLAG_HIDDEN);
    } else {
        waterfall_clear();
        lv_obj_add_flag(s_bars_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_wf, LV_OBJ_FLAG_HIDDEN);
    }
}

/** Plot clicked: touch tap or encoder push (the plot is the default focus). */
static void toggle_cb(lv_event_t *e)
{
    (void)e;
    set_mode(s_mode == MODE_BARS ? MODE_WATERFALL : MODE_BARS);
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_show_home();
}

static void axis_label(lv_obj_t *parent, float hz, const char *text)
{
    int band = (int)(logf(hz / F_MIN) / logf(F_MAX / F_MIN) * GIZMO_SPECTRUM_BANDS);
    if (band < 0) band = 0;
    if (band >= GIZMO_SPECTRUM_BANDS) band = GIZMO_SPECTRUM_BANDS - 1;

    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_SUBTEXT, 0);
    lv_obj_update_layout(lbl);
    int32_t x = bar_x(band) + bar_w() / 2 - lv_obj_get_width(lbl) / 2;
    if (x < 2) x = 2;
    if (x > 318 - lv_obj_get_width(lbl)) x = 318 - lv_obj_get_width(lbl);
    lv_obj_set_pos(lbl, x, AREA_Y + AREA_H + 4);
}

static lv_obj_t *plain_rect(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* ---------- public ---------- */

lv_group_t *ui_spectrum_group(void)
{
    return s_group;
}

void ui_spectrum_on_show(void)
{
    for (int i = 0; i < GIZMO_SPECTRUM_BANDS; i++) {
        s_disp[i] = s_peak[i] = 0;
        s_hold[i] = 0;
    }
    waterfall_clear();
    /* Encoder push toggles the view right away; rotate to reach "back". */
    lv_group_focus_obj(s_toggle);

    esp_err_t err = gizmo_core_spectrum_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Spectrum start failed: %s", esp_err_to_name(err));
        lv_label_set_text(s_info, "Microphone not available");
        lv_obj_set_style_text_color(s_info, UI_COLOR_LOW, 0);
        return;
    }
    lv_obj_set_style_text_color(s_info, UI_COLOR_TEXT, 0);
    lv_label_set_text(s_info, "Listening...");
    s_running = true;
    lv_timer_resume(s_timer);
}

void ui_spectrum_on_hide(void)
{
    if (!s_running) {
        return;
    }
    s_running = false;
    lv_timer_pause(s_timer);
    gizmo_core_spectrum_stop();
}

lv_obj_t *ui_spectrum_create(void)
{
    s_scr = ui_create_screen();
    s_group = lv_group_create();

    lv_obj_t *hdr = ui_create_header(s_scr, "Spectrum");
    ui_create_back_button(hdr, back_cb);

    s_info = lv_label_create(s_scr);
    lv_obj_set_pos(s_info, AREA_X, UI_HEADER_H + 6);
    lv_label_set_text(s_info, "");

    s_input = lv_label_create(s_scr);
    lv_obj_align(s_input, LV_ALIGN_TOP_RIGHT, -AREA_X, UI_HEADER_H + 6);
    lv_label_set_text(s_input, "");
    lv_obj_set_style_text_color(s_input, UI_COLOR_SUBTEXT, 0);

    /* Bars view: full-screen transparent box so child coordinates stay absolute. */
    s_bars_box = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_bars_box);
    lv_obj_set_size(s_bars_box, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(s_bars_box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < GIZMO_SPECTRUM_BANDS; i++) {
        lv_obj_t *bar = plain_rect(s_bars_box, bar_x(i), AREA_Y + AREA_H - 1, bar_w(), 1,
                                   lv_color_hex(0xff5f57));
        /* Vertical gradient: hot at the top, green at the bottom. */
        lv_obj_set_style_bg_grad_color(bar, lv_color_hex(0x4cd964), 0);
        lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_radius(bar, 1, 0);
        s_bars[i] = bar;
        s_peaks[i] = plain_rect(s_bars_box, bar_x(i), AREA_Y + AREA_H - 2, bar_w(), 2, UI_COLOR_TEXT);
    }

    /* Waterfall view */
    build_lut();
    s_wf_buf = heap_caps_calloc((size_t)AREA_W * AREA_H, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    s_wf = lv_canvas_create(s_scr);
    if (s_wf_buf) {
        lv_canvas_set_buffer(s_wf, s_wf_buf, AREA_W, AREA_H, LV_COLOR_FORMAT_RGB565);
    } else {
        ESP_LOGE(TAG, "No memory for waterfall buffer");
    }
    lv_obj_set_pos(s_wf, AREA_X, AREA_Y);
    lv_obj_remove_flag(s_wf, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wf, LV_OBJ_FLAG_HIDDEN);

    /* Transparent button over the plot: tap or encoder push toggles the
     * view. Created after both views so it sits on top. */
    s_toggle = lv_button_create(s_scr);
    lv_obj_remove_style_all(s_toggle);
    lv_obj_set_size(s_toggle, AREA_W, AREA_H);
    lv_obj_set_pos(s_toggle, AREA_X, AREA_Y);
    lv_obj_add_event_cb(s_toggle, toggle_cb, LV_EVENT_CLICKED, NULL);

    /* Encoder order: back button (header), then the plot. */
    lv_obj_t *roots[] = { hdr, s_toggle };
    ui_group_rebuild(s_group, roots, 2);

    /* Baseline and frequency axis (shared by both views) */
    plain_rect(s_scr, AREA_X, AREA_Y + AREA_H, AREA_W, 1, UI_COLOR_CARD);
    axis_label(s_scr, 100.0f, "100");
    axis_label(s_scr, 1000.0f, "1k");
    axis_label(s_scr, 4000.0f, "4k");
    axis_label(s_scr, 7900.0f, "8k");

    s_timer = lv_timer_create(frame_cb, FRAME_MS, NULL);
    lv_timer_pause(s_timer);
    return s_scr;
}
