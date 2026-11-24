/*
 * "Life" screen: 2D outer-totalistic cellular automata (Moore neighbourhood).
 *
 * The grid is GRID_W x GRID_H cells on a torus (edges wrap), drawn as
 * CELL x CELL blocks on a full-screen RGB565 canvas in PSRAM. Only cells that
 * change are repainted. Cells born this generation are drawn brighter than
 * survivors, so growth fronts stand out.
 *
 * Rules are B/S bitmasks (bit n = n live neighbours):
 *   Life B3/S23, HighLife B36/S23, Day & Night B3678/S34678, Seeds B2/S,
 *   Maze B3/S12345.
 *
 * Input (one invisible full-screen pad, so touch and encoder behave alike):
 *   tap / encoder push        new random soup
 *   long press (touch/encoder) next rule
 * A soup that dies out or freezes is replaced automatically. Back = HMI
 * button 1 or swipe right. The simulation only runs while the page is shown.
 */
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ui_priv.h"

static const char *TAG = "ui_life";

#define CELL        4
#define GRID_W      (320 / CELL)
#define GRID_H      (240 / CELL)
#define GEN_MS      100
#define SOUP_PCT    30             /* initial density, percent */
#define STALL_GENS  40             /* reseed after this many gens without change */
#define HINT_GENS   30             /* how long the hint stays up */

typedef struct {
    const char *name;
    uint16_t birth;     /* bit n set: dead cell with n neighbours is born */
    uint16_t survive;   /* bit n set: live cell with n neighbours survives */
} life_rule_t;

#define B(n) (1u << (n))
static const life_rule_t s_rules[] = {
    { "Life B3/S23",          B(3),                   B(2) | B(3) },
    { "HighLife B36/S23",     B(3) | B(6),            B(2) | B(3) },
    { "Day&Night B3678/S34678", B(3) | B(6) | B(7) | B(8), B(3) | B(4) | B(6) | B(7) | B(8) },
    { "Seeds B2/S",           B(2),                   0 },
    { "Maze B3/S12345",       B(3),                   B(1) | B(2) | B(3) | B(4) | B(5) },
};
#define RULE_COUNT ((int)(sizeof(s_rules) / sizeof(s_rules[0])))

/* Cell states: 0 dead, 1 survivor, 2 newborn (drawn brighter). */
static uint8_t s_cur[GRID_H][GRID_W];
static uint8_t s_next[GRID_H][GRID_W];

static lv_obj_t *s_scr;
static lv_group_t *s_group;
static lv_timer_t *s_timer;
static lv_obj_t *s_canvas;
static uint16_t *s_buf;            /* 320 x 240 RGB565 */
static lv_obj_t *s_info;
static lv_obj_t *s_hint;

static int s_rule;
static uint32_t s_gen;
static uint32_t s_pop;
static uint32_t s_stall;
static uint16_t s_col_dead, s_col_old, s_col_new;

/* ---------- drawing ---------- */

static void paint_cell(int x, int y, uint8_t state)
{
    uint16_t c = state == 0 ? s_col_dead : (state == 2 ? s_col_new : s_col_old);
    uint16_t *p = s_buf + (y * CELL) * 320 + x * CELL;
    for (int r = 0; r < CELL; r++, p += 320) {
        /* 1 px gap on the right/bottom gives a subtle grid look for live cells. */
        int w = state ? CELL - 1 : CELL;
        for (int k = 0; k < w; k++) {
            p[k] = c;
        }
        if (state) {
            p[CELL - 1] = s_col_dead;
        }
    }
    if (state) {
        uint16_t *last = s_buf + (y * CELL + CELL - 1) * 320 + x * CELL;
        for (int k = 0; k < CELL; k++) {
            last[k] = s_col_dead;
        }
    }
}

static void paint_all(void)
{
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            paint_cell(x, y, s_cur[y][x]);
        }
    }
    lv_obj_invalidate(s_canvas);
}

static void update_info(void)
{
    lv_label_set_text_fmt(s_info, "%s  gen %lu  pop %lu", s_rules[s_rule].name,
                          (unsigned long)s_gen, (unsigned long)s_pop);
}

/* ---------- simulation ---------- */

static void seed(void)
{
    s_pop = 0;
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            bool alive = lv_rand(0, 99) < SOUP_PCT;
            s_cur[y][x] = alive ? 2 : 0;
            s_pop += alive;
        }
    }
    s_gen = 0;
    s_stall = 0;
    paint_all();
    update_info();
}

static void step(void)
{
    const life_rule_t *r = &s_rules[s_rule];
    uint32_t pop = 0, changed = 0;

    for (int y = 0; y < GRID_H; y++) {
        const uint8_t *up = s_cur[(y + GRID_H - 1) % GRID_H];
        const uint8_t *mid = s_cur[y];
        const uint8_t *dn = s_cur[(y + 1) % GRID_H];
        for (int x = 0; x < GRID_W; x++) {
            int l = (x + GRID_W - 1) % GRID_W, rr = (x + 1) % GRID_W;
            int n = (up[l] != 0) + (up[x] != 0) + (up[rr] != 0) +
                    (mid[l] != 0) + (mid[rr] != 0) +
                    (dn[l] != 0) + (dn[x] != 0) + (dn[rr] != 0);
            bool alive = mid[x] != 0;
            uint8_t nxt;
            if (alive) {
                nxt = (r->survive & (1u << n)) ? 1 : 0;
            } else {
                nxt = (r->birth & (1u << n)) ? 2 : 0;
            }
            s_next[y][x] = nxt;
            pop += nxt != 0;
            /* Repaint only on a visible change (dead<->alive or newborn->old). */
            if (nxt != mid[x]) {
                paint_cell(x, y, nxt);
                if ((nxt != 0) != alive) {
                    changed++;
                }
            }
        }
    }
    memcpy(s_cur, s_next, sizeof(s_cur));
    lv_obj_invalidate(s_canvas);

    s_gen++;
    s_stall = (changed == 0 || pop == s_pop) ? s_stall + 1 : 0;
    s_pop = pop;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    step();
    if (s_pop == 0 || s_stall >= STALL_GENS) {
        seed();     /* died out or frozen: start a new soup */
    }
    if (s_gen == HINT_GENS) {
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    }
    update_info();
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
    s_rule = (s_rule + 1) % RULE_COUNT;
    ESP_LOGI(TAG, "Rule: %s", s_rules[s_rule].name);
    seed();
}

/* ---------- public ---------- */

lv_group_t *ui_life_group(void)
{
    return s_group;
}

void ui_life_on_show(void)
{
    if (!s_buf) {
        return;
    }
    lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    seed();
    lv_timer_resume(s_timer);
}

void ui_life_on_hide(void)
{
    if (s_timer) {
        lv_timer_pause(s_timer);
    }
}

lv_obj_t *ui_life_create(void)
{
    s_scr = ui_create_screen();
    s_group = lv_group_create();
    ui_enable_back_gesture(s_scr);

    s_col_dead = lv_color_to_u16(UI_COLOR_BG);
    s_col_old = lv_color_to_u16(UI_COLOR_ACCENT);
    s_col_new = lv_color_to_u16(UI_COLOR_OK);

    s_buf = heap_caps_calloc(320 * 240, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    s_canvas = lv_canvas_create(s_scr);
    if (s_buf) {
        lv_canvas_set_buffer(s_canvas, s_buf, 320, 240, LV_COLOR_FORMAT_RGB565);
    } else {
        ESP_LOGE(TAG, "No memory for the canvas");
    }
    lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);

    /* Status line over the grid, on a translucent strip for readability. */
    s_info = lv_label_create(s_scr);
    lv_obj_align(s_info, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_width(s_info, LV_PCT(100));
    lv_obj_set_style_bg_color(s_info, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_info, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(s_info, 6, 0);
    lv_obj_set_style_pad_ver(s_info, 2, 0);
    lv_obj_set_style_text_color(s_info, UI_COLOR_TEXT, 0);

    s_hint = lv_label_create(s_scr);
    lv_label_set_text(s_hint, "Tap: new soup   Hold: next rule");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_color(s_hint, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_hint, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(s_hint, 8, 0);
    lv_obj_set_style_pad_ver(s_hint, 3, 0);
    lv_obj_set_style_radius(s_hint, 6, 0);
    lv_obj_set_style_text_color(s_hint, UI_COLOR_TEXT, 0);

    /* Invisible full-screen pad: tap / encoder push = new soup, hold = next rule. */
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
