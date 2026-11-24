#include "gizmo_ui.h"

#include "esp_log.h"
#include "ui_priv.h"

static const char *TAG = "ui";

static lv_obj_t *s_home_scr;
static lv_obj_t *s_settings_scr;
static lv_obj_t *s_i2c_scr;
static lv_group_t *s_home_group;

/* ---------- encoder groups ---------- */

void ui_use_group(lv_group_t *group)
{
    lv_indev_t *enc = gizmo_core_get_encoder();
    if (enc) {
        lv_group_set_editing(group, false);
        lv_indev_set_group(enc, group);
    }
}

/**
 * Module HMI button 1 sends ESC to the focused widget: go back to home.
 * Attached to every widget collected by ui_group_rebuild(), since key events
 * don't bubble to the screen.
 */
static void esc_back_cb(lv_event_t *e)
{
    if (lv_event_get_key(e) == LV_KEY_ESC && lv_screen_active() != s_home_scr) {
        ui_show_home();
    }
}

typedef struct {
    lv_group_t *group;
} collect_ctx_t;

static lv_obj_tree_walk_res_t collect_cb(lv_obj_t *obj, void *user_data)
{
    collect_ctx_t *ctx = user_data;

    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    }
    if (lv_obj_has_class(obj, &lv_button_class) ||
        lv_obj_has_class(obj, &lv_slider_class) ||
        lv_obj_has_class(obj, &lv_dropdown_class) ||
        lv_obj_has_class(obj, &lv_buttonmatrix_class)) {
        ui_add_focusable(ctx->group, obj);
        lv_obj_remove_event_cb(obj, esc_back_cb);   /* avoid duplicates on rebuild */
        lv_obj_add_event_cb(obj, esc_back_cb, LV_EVENT_KEY, NULL);
        return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

void ui_group_rebuild(lv_group_t *group, lv_obj_t *const *roots, int n_roots)
{
    lv_obj_t *focused = lv_group_get_focused(group);
    bool editing = lv_group_get_editing(group);

    lv_group_remove_all_objs(group);
    collect_ctx_t ctx = { .group = group };
    for (int i = 0; i < n_roots; i++) {
        if (roots[i]) {
            lv_obj_tree_walk(roots[i], collect_cb, &ctx);
        }
    }

    /* Restore focus if that widget survived (e.g. the tab button just used). */
    if (focused && lv_obj_get_group(focused) == group) {
        lv_group_focus_obj(focused);
        lv_group_set_editing(group, editing);
    }
}

/* ---------- navigation ---------- */

void ui_show_home(void)
{
    /* Persist on leave as well: the encoder back button skips the slider's
     * release event, which is where touch input saves. */
    esp_err_t err = gizmo_core_set_brightness(gizmo_core_get_brightness(), true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save brightness: %s", esp_err_to_name(err));
    }
    ui_use_group(s_home_group);
    lv_screen_load_anim(s_home_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, UI_ANIM_MS, 0, false);
}

void ui_show_settings(void)
{
    ui_settings_on_show();
    ui_use_group(ui_settings_group());
    lv_screen_load_anim(s_settings_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, UI_ANIM_MS, 0, false);
}

void ui_show_i2c(void)
{
    ui_i2c_on_show();
    ui_use_group(ui_i2c_group());
    lv_screen_load_anim(s_i2c_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, UI_ANIM_MS, 0, false);
}

/* ---------- public ---------- */

void gizmo_ui_init(void)
{
    ui_theme_init();

    s_home_group = lv_group_create();
    s_home_scr = ui_home_create(s_home_group);
    s_settings_scr = ui_settings_create();
    s_i2c_scr = ui_i2c_create();

    ui_status_start();
    ui_use_group(s_home_group);
    lv_screen_load(s_home_scr);
}
