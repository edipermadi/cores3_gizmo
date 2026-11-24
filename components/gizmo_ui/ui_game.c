/*
 * "Hopper": an original endless-runner mini game.
 *
 * A little robot runs along the ground; tap the screen (or push the encoder)
 * to hop over crates. Speed rises slowly; the score grows with distance and
 * the best score is saved ("hop_best"). Full screen like other pages: back =
 * HMI button 1 or swipe right. The game only ticks while the page is shown.
 *
 * Everything is plain LVGL objects moved by a 30 fps timer (no sprites):
 *   robot  rounded body, eye, antenna, two legs that alternate while running
 *   world  ground line, scrolling ground specks, slow clouds
 *   crates pool of MAX_CRATES rectangles recycled from the right edge
 */
#include <stdio.h>

#include "ui_priv.h"

#define PREF_BEST     "hop_best"
#define FRAME_MS      33

#define SCR_W         320
#define GROUND_Y      196            /* top of the ground line */
#define PLAYER_X      48
#define PLAYER_W      24
#define PLAYER_H      26

#define GRAVITY       1.1f           /* px / frame^2 */
#define JUMP_V        (-13.0f)       /* px / frame: ~77 px apex, ~0.8 s airtime */
#define SPEED_START   5.0f           /* px / frame */
#define SPEED_MAX     11.0f
#define SPEED_GAIN    0.0025f        /* per frame */
#define HITBOX_INSET  3

#define MAX_CRATES    4
#define SPECKS        10
#define CLOUDS        2

typedef enum { ST_READY, ST_RUN, ST_OVER } game_state_t;

typedef struct {
    lv_obj_t *obj;
    float x;
    int w, h;
    bool active;
} crate_t;

static lv_obj_t *s_scr;
static lv_group_t *s_group;
static lv_timer_t *s_timer;

static lv_obj_t *s_robot, *s_leg_l, *s_leg_r;
static lv_obj_t *s_specks[SPECKS];
static float s_speck_x[SPECKS];
static lv_obj_t *s_clouds[CLOUDS];
static float s_cloud_x[CLOUDS];
static crate_t s_crates[MAX_CRATES];

static lv_obj_t *s_score_lbl, *s_best_lbl, *s_msg_lbl, *s_hint_lbl;

static game_state_t s_state;
static float s_y;          /* robot top-left y offset above ground (0 = on ground) */
static float s_vy;
static float s_speed;
static float s_score;
static uint32_t s_best;
static float s_next_gap;   /* px until the next crate group spawns */
static uint32_t s_frame;
static uint32_t s_over_frame;

/* ---------- helpers ---------- */

static lv_obj_t *rect(lv_obj_t *parent, int w, int h, lv_color_t c, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void show_message(const char *msg, const char *hint)
{
    lv_label_set_text(s_msg_lbl, msg);
    lv_label_set_text(s_hint_lbl, hint);
    lv_obj_remove_flag(s_msg_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_hint_lbl, LV_OBJ_FLAG_HIDDEN);
}

static void hide_message(void)
{
    lv_obj_add_flag(s_msg_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_hint_lbl, LV_OBJ_FLAG_HIDDEN);
}

static void update_score_labels(void)
{
    lv_label_set_text_fmt(s_score_lbl, "%05lu", (unsigned long)s_score);
    lv_label_set_text_fmt(s_best_lbl, "BEST %05lu", (unsigned long)s_best);
}

static void place_robot(void)
{
    int top = GROUND_Y - PLAYER_H - (int)s_y;
    lv_obj_set_pos(s_robot, PLAYER_X, top);
}

/* ---------- crates ---------- */

static float rand_gap(void)
{
    /* Wider gaps at higher speed so jumps stay possible. */
    return (float)lv_rand(150, 290) + s_speed * 12.0f;
}

static void spawn_crate(int w, int h, float x)
{
    for (int i = 0; i < MAX_CRATES; i++) {
        crate_t *c = &s_crates[i];
        if (!c->active) {
            c->active = true;
            c->x = x;
            c->w = w;
            c->h = h;
            lv_obj_set_size(c->obj, w, h);
            lv_obj_set_pos(c->obj, (int)x, GROUND_Y - h);
            lv_obj_remove_flag(c->obj, LV_OBJ_FLAG_HIDDEN);
            return;
        }
    }
}

static void spawn_group(void)
{
    switch (lv_rand(0, 2)) {
    case 0:  spawn_crate(20, 20, SCR_W); break;                                 /* small */
    case 1:  spawn_crate(20, 34, SCR_W); break;                                 /* tall */
    default: spawn_crate(20, 20, SCR_W); spawn_crate(20, 20, SCR_W + 22); break; /* double */
    }
}

static bool hits(const crate_t *c)
{
    int rx0 = PLAYER_X + HITBOX_INSET, rx1 = PLAYER_X + PLAYER_W - HITBOX_INSET;
    int ry1 = GROUND_Y - (int)s_y - HITBOX_INSET;           /* robot bottom */
    int cx0 = (int)c->x + HITBOX_INSET, cx1 = (int)c->x + c->w - HITBOX_INSET;
    int cy0 = GROUND_Y - c->h + HITBOX_INSET;               /* crate top */
    return rx1 > cx0 && rx0 < cx1 && ry1 > cy0;
}

/* ---------- game flow ---------- */

static void reset_world(void)
{
    s_y = 0;
    s_vy = 0;
    s_speed = SPEED_START;
    s_score = 0;
    s_next_gap = 120;
    for (int i = 0; i < MAX_CRATES; i++) {
        s_crates[i].active = false;
        lv_obj_add_flag(s_crates[i].obj, LV_OBJ_FLAG_HIDDEN);
    }
    place_robot();
    update_score_labels();
}

static void start_game(void)
{
    reset_world();
    hide_message();
    s_state = ST_RUN;
}

static void game_over(void)
{
    s_state = ST_OVER;
    s_over_frame = s_frame;
    if ((uint32_t)s_score > s_best) {
        s_best = (uint32_t)s_score;
        gizmo_core_pref_set_u32(PREF_BEST, s_best);
        show_message("New best!", "Tap to play again");
    } else {
        show_message("Game over", "Tap to play again");
    }
    update_score_labels();
}

static void press_cb(lv_event_t *e)
{
    (void)e;
    switch (s_state) {
    case ST_READY:
        start_game();
        s_vy = JUMP_V;
        break;
    case ST_RUN:
        if (s_y <= 0.0f) {      /* only from the ground */
            s_vy = JUMP_V;
        }
        break;
    case ST_OVER:
        /* Ignore taps right after a crash so a late jump tap doesn't restart. */
        if (s_frame - s_over_frame > 15) {
            start_game();
        }
        break;
    }
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    s_frame++;

    /* Clouds drift even on the start / game-over screens. */
    for (int i = 0; i < CLOUDS; i++) {
        s_cloud_x[i] -= 0.4f;
        if (s_cloud_x[i] < -60) {
            s_cloud_x[i] = SCR_W + lv_rand(0, 80);
        }
        lv_obj_set_x(s_clouds[i], (int)s_cloud_x[i]);
    }
    if (s_state != ST_RUN) {
        return;
    }

    s_speed += SPEED_GAIN;
    if (s_speed > SPEED_MAX) {
        s_speed = SPEED_MAX;
    }
    s_score += s_speed / 8.0f;

    /* Robot physics */
    s_vy += GRAVITY;
    s_y -= s_vy;
    if (s_y <= 0) {
        s_y = 0;
        s_vy = 0;
    }
    place_robot();

    /* Legs alternate while on the ground, tuck in the air. */
    bool step = (s_frame / 3) & 1;
    lv_obj_set_height(s_leg_l, s_y > 0 ? 3 : (step ? 6 : 3));
    lv_obj_set_height(s_leg_r, s_y > 0 ? 3 : (step ? 3 : 6));

    /* Ground specks */
    for (int i = 0; i < SPECKS; i++) {
        s_speck_x[i] -= s_speed;
        if (s_speck_x[i] < -4) {
            s_speck_x[i] += SCR_W + 8;
        }
        lv_obj_set_x(s_specks[i], (int)s_speck_x[i]);
    }

    /* Crates */
    s_next_gap -= s_speed;
    if (s_next_gap <= 0) {
        spawn_group();
        s_next_gap = rand_gap();
    }
    for (int i = 0; i < MAX_CRATES; i++) {
        crate_t *c = &s_crates[i];
        if (!c->active) {
            continue;
        }
        c->x -= s_speed;
        if (c->x < -c->w) {
            c->active = false;
            lv_obj_add_flag(c->obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_x(c->obj, (int)c->x);
        if (hits(c)) {
            game_over();
            return;
        }
    }

    if ((s_frame & 3) == 0) {
        update_score_labels();
    }
}

/* ---------- public ---------- */

lv_group_t *ui_game_group(void)
{
    return s_group;
}

void ui_game_on_show(void)
{
    s_state = ST_READY;
    reset_world();
    show_message("Hopper", "Tap to jump");
    lv_timer_resume(s_timer);
}

void ui_game_on_hide(void)
{
    lv_timer_pause(s_timer);
    s_state = ST_READY;
}

lv_obj_t *ui_game_create(void)
{
    s_scr = ui_create_screen();
    s_group = lv_group_create();
    ui_enable_back_gesture(s_scr);
    s_best = gizmo_core_pref_get_u32(PREF_BEST, 0);

    /* Clouds (behind everything) */
    for (int i = 0; i < CLOUDS; i++) {
        s_clouds[i] = rect(s_scr, 48, 14, UI_COLOR_CARD, 7);
        s_cloud_x[i] = 80 + i * 150;
        lv_obj_set_pos(s_clouds[i], (int)s_cloud_x[i], 60 + i * 34);
    }

    /* Ground line and specks */
    lv_obj_t *ground = rect(s_scr, SCR_W, 2, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(ground, 0, GROUND_Y);
    for (int i = 0; i < SPECKS; i++) {
        s_specks[i] = rect(s_scr, (i % 3) + 2, 2, UI_COLOR_SUBTEXT, 0);
        s_speck_x[i] = (float)(i * SCR_W / SPECKS + lv_rand(0, 20));
        lv_obj_set_pos(s_specks[i], (int)s_speck_x[i], GROUND_Y + 6 + (i % 4) * 6);
    }

    /* Crates */
    for (int i = 0; i < MAX_CRATES; i++) {
        lv_obj_t *c = rect(s_scr, 20, 20, lv_color_hex(0xc98a3b), 2);
        lv_obj_set_style_border_width(c, 2, 0);
        lv_obj_set_style_border_color(c, lv_color_hex(0x8a5a22), 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
        s_crates[i].obj = c;
    }

    /* Robot: body with eye and antenna; legs below it. */
    s_robot = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_robot);
    lv_obj_set_size(s_robot, PLAYER_W, PLAYER_H + 8);    /* + antenna room above */
    lv_obj_remove_flag(s_robot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_translate_y(s_robot, -8, 0);         /* antenna sits above the hitbox */
    lv_obj_t *body = rect(s_robot, PLAYER_W, PLAYER_H - 6, UI_COLOR_ACCENT, 6);
    lv_obj_set_pos(body, 0, 8);
    lv_obj_t *eye = rect(body, 8, 8, lv_color_white(), 2);
    lv_obj_set_pos(eye, 12, 4);
    lv_obj_t *pupil = rect(eye, 3, 4, lv_color_hex(0x1c232b), 1);
    lv_obj_set_pos(pupil, 4, 2);
    lv_obj_t *mast = rect(s_robot, 2, 7, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(mast, 7, 2);
    lv_obj_t *tip = rect(s_robot, 5, 5, UI_COLOR_LOW, 3);
    lv_obj_set_pos(tip, 5, 0);
    s_leg_l = rect(s_robot, 5, 6, UI_COLOR_SUBTEXT, 1);
    lv_obj_set_pos(s_leg_l, 4, PLAYER_H + 2);
    s_leg_r = rect(s_robot, 5, 6, UI_COLOR_SUBTEXT, 1);
    lv_obj_set_pos(s_leg_r, 15, PLAYER_H + 2);

    /* Score */
    s_best_lbl = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_best_lbl, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_pos(s_best_lbl, 10, 8);
    s_score_lbl = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_score_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(s_score_lbl, LV_ALIGN_TOP_RIGHT, -10, 6);

    /* Messages */
    s_msg_lbl = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_msg_lbl, &lv_font_montserrat_28, 0);
    lv_obj_align(s_msg_lbl, LV_ALIGN_CENTER, 0, -40);
    s_hint_lbl = lv_label_create(s_scr);
    lv_obj_set_style_text_color(s_hint_lbl, UI_COLOR_SUBTEXT, 0);
    lv_obj_align(s_hint_lbl, LV_ALIGN_CENTER, 0, -10);

    /* Invisible full-screen pad on top: press = jump (touch or encoder push). */
    lv_obj_t *pad = lv_button_create(s_scr);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, LV_PCT(100), LV_PCT(100));
    lv_obj_add_event_cb(pad, press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_t *roots[] = { pad };
    ui_group_rebuild(s_group, roots, 1);

    s_timer = lv_timer_create(tick_cb, FRAME_MS, NULL);
    lv_timer_pause(s_timer);
    reset_world();
    return s_scr;
}
