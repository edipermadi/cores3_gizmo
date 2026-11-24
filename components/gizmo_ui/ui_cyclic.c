/*
 * "Cyclic" screen: cyclic cellular automaton (Griffeath's CCA).
 *
 * Every cell holds a state 0..N-1. A cell in state s advances to (s+1) mod N
 * when at least T of its neighbours are already in state s+1; otherwise it
 * stays. From random noise this self-organises into rotating spirals and
 * travelling waves. Each state is drawn with its own hue.
 *
 * Grid GRID_W x GRID_H cells of CELL px, wrapping at the edges, on a
 * full-screen RGB565 canvas in PSRAM; only changed cells are repainted.
 *
 * Presets (long press cycles):
 *   Spirals 313   Moore,        3 states, threshold 3
 *   Cyclic 14     von Neumann, 14 states, threshold 1
 *   Waves 8       Moore,        8 states, threshold 2
 *
 * Input via one invisible full-screen pad (touch and encoder alike):
 * tap / push = new random noise, long press = next preset. A grid that stops
 * changing is reseeded. Back = HMI button 1 or swipe right. Runs only while
 * the page is shown.
 */
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ui_priv.h"

static const char *TAG = "ui_cyclic";

#define CELL        2
#define GRID_W      (320 / CELL)
#define GRID_H      (240 / CELL)
#define GEN_MS      50
#define MAX_STATES  16
#define HINT_GENS   60
#define STALL_GENS  20

typedef struct {
    const char *name;
    uint8_t states;     /* N */
    uint8_t threshold;  /* T */
    bool moore;         /* 8 neighbours (else von Neumann, 4) */
} cca_preset_t;

static const cca_preset_t s_presets[] = {
    { "Spirals 313",  3, 3, true },
    { "Cyclic 14",   14, 1, false },
    { "Waves 8",      8, 2, true },
};
#define PRESET_COUNT ((int)(sizeof(s_presets) / sizeof(s_presets[0])))

static uint8_t *s_cur;             /* GRID_W * GRID_H, PSRAM */
static uint8_t *s_next;
static uint16_t *s_buf;            /* 320 x 240 RGB565 canvas, PSRAM */
static uint16_t s_colors[MAX_STATES];

static lv_obj_t *s_scr;
static lv_group_t *s_group;
static lv_timer_t *s_timer;
static lv_obj_t *s_canvas;
static lv_obj_t *s_info;
static lv_obj_t *s_hint;

static int s_preset;
static uint32_t s_gen;
static uint32_t s_stall;

#define AT(g, x, y) ((g)[(y) * GRID_W + (x)])

/* ---------- drawing ---------- */

static void build_colors(void)
{
    const cca_preset_t *p = &s_presets[s_preset];
    for (int i = 0; i < p->states; i++) {
        /* Evenly spaced hues; fully saturated, bright enough for both themes. */
        lv_color_t c = lv_color_hsv_to_rgb((uint16_t)(i * 360 / p->states), 85, 95);
        s_colors[i] = lv_color_to_u16(c);
    }
}

static inline void paint_cell(int x, int y, uint8_t state)
{
    uint16_t c = s_colors[state];
    uint16_t *p = s_buf + (y * CELL) * 320 + x * CELL;
    for (int r = 0; r < CELL; r++, p += 320) {
        for (int k = 0; k < CELL; k++) {
            p[k] = c;
        }
    }
}

static void paint_all(void)
{
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            paint_cell(x, y, AT(s_cur, x, y));
        }
    }
    lv_obj_invalidate(s_canvas);
}

static void update_info(void)
{
    const cca_preset_t *p = &s_presets[s_preset];
    /* e.g. "Cyclic 14  N14 T1 vN  gen 376" (vN = von Neumann, M = Moore) */
    lv_label_set_text_fmt(s_info, "%s  N%d T%d %s  gen %lu", p->name, p->states, p->threshold,
                          p->moore ? "M" : "vN", (unsigned long)s_gen);
}

/* ---------- simulation ---------- */

static void seed(void)
{
    const uint8_t n = s_presets[s_preset].states;
    for (int i = 0; i < GRID_W * GRID_H; i++) {
        s_cur[i] = (uint8_t)lv_rand(0, n - 1);
    }
    s_gen = 0;
    s_stall = 0;
    build_colors();
    paint_all();
    update_info();
}

static void step(void)
{
    const cca_preset_t *p = &s_presets[s_preset];
    uint32_t changed = 0;

    for (int y = 0; y < GRID_H; y++) {
        const int yu = (y + GRID_H - 1) % GRID_H, yd = (y + 1) % GRID_H;
        for (int x = 0; x < GRID_W; x++) {
            const int xl = (x + GRID_W - 1) % GRID_W, xr = (x + 1) % GRID_W;
            uint8_t s = AT(s_cur, x, y);
            uint8_t succ = (uint8_t)((s + 1) % p->states);

            int cnt = (AT(s_cur, x, yu) == succ) + (AT(s_cur, x, yd) == succ) +
                      (AT(s_cur, xl, y) == succ) + (AT(s_cur, xr, y) == succ);
            if (p->moore) {
                cnt += (AT(s_cur, xl, yu) == succ) + (AT(s_cur, xr, yu) == succ) +
                       (AT(s_cur, xl, yd) == succ) + (AT(s_cur, xr, yd) == succ);
            }

            uint8_t nxt = cnt >= p->threshold ? succ : s;
            AT(s_next, x, y) = nxt;
            if (nxt != s) {
                paint_cell(x, y, nxt);
                changed++;
            }
        }
    }
    uint8_t *t = s_cur;
    s_cur = s_next;
    s_next = t;
    lv_obj_invalidate(s_canvas);

    s_gen++;
    s_stall = changed == 0 ? s_stall + 1 : 0;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    step();
    if (s_stall >= STALL_GENS) {
        seed();     /* frozen: start over */
    }
    if (s_gen == HINT_GENS) {
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    }
    if ((s_gen & 3) == 0) {
        update_info();
    }
}

/* ---------- input ---------- */

static void tap_cb(lv_event_t *e)
{
    (void)e;
    seed();
}

static void long_press_cb(lv_event_t *e)
{
    (void)e;
    s_preset = (s_preset + 1) % PRESET_COUNT;
    ESP_LOGI(TAG, "Preset: %s", s_presets[s_preset].name);
    seed();
}

/* ---------- public ---------- */

lv_group_t *ui_cyclic_group(void)
{
    return s_group;
}

void ui_cyclic_on_show(void)
{
    if (!s_buf || !s_cur || !s_next) {
        return;
    }
    lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    seed();
    lv_timer_resume(s_timer);
}

void ui_cyclic_on_hide(void)
{
    if (s_timer) {
        lv_timer_pause(s_timer);
    }
}

lv_obj_t *ui_cyclic_create(void)
{
    s_scr = ui_create_screen();
    s_group = lv_group_create();
    ui_enable_back_gesture(s_scr);

    /* Grids in PSRAM: internal RAM is needed by Wi-Fi, LVGL and the console.
     * Each grid is ~19 KB, so it stays mostly in the data cache. */
    s_cur = heap_caps_malloc(GRID_W * GRID_H, MALLOC_CAP_SPIRAM);
    s_next = heap_caps_malloc(GRID_W * GRID_H, MALLOC_CAP_SPIRAM);
    s_buf = heap_caps_calloc(320 * 240, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_cur || !s_next || !s_buf) {
        ESP_LOGE(TAG, "Out of memory");
    }

    s_canvas = lv_canvas_create(s_scr);
    if (s_buf) {
        lv_canvas_set_buffer(s_canvas, s_buf, 320, 240, LV_COLOR_FORMAT_RGB565);
    }
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);

    s_info = lv_label_create(s_scr);
    lv_obj_align(s_info, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_width(s_info, LV_PCT(100));
    lv_label_set_long_mode(s_info, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_bg_color(s_info, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_info, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(s_info, 6, 0);
    lv_obj_set_style_pad_ver(s_info, 2, 0);
    lv_obj_set_style_text_color(s_info, UI_COLOR_TEXT, 0);

    s_hint = lv_label_create(s_scr);
    lv_label_set_text(s_hint, "Tap: new noise   Hold: next preset");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(s_hint, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(s_hint, 8, 0);
    lv_obj_set_style_pad_ver(s_hint, 3, 0);
    lv_obj_set_style_radius(s_hint, 6, 0);
    lv_obj_set_style_text_color(s_hint, UI_COLOR_TEXT, 0);

    lv_obj_t *pad = lv_button_create(s_scr);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, LV_PCT(100), LV_PCT(100));
    lv_obj_add_event_cb(pad, tap_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(pad, long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_t *roots[] = { pad };
    ui_group_rebuild(s_group, roots, 1);

    s_timer = lv_timer_create(tick_cb, GEN_MS, NULL);
    lv_timer_pause(s_timer);
    return s_scr;
}
