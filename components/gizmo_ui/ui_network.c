/*
 * "Network" tab: Wi-Fi status, scan results, connect / forget.
 *
 * Layout (tab page):
 *   [status text ..................] [refresh] [forget]
 *   [network list (scrollable)                       ]
 *
 * Secured networks open a full-screen password dialog on the top layer with an
 * on-screen keyboard. The keyboard gets its own encoder group (edit mode), so
 * the encoder moves between keys and push types; HMI button 1 cancels.
 *
 * All state comes from gizmo_core's cached Wi-Fi status, polled by an LVGL
 * timer, so nothing here blocks on the network.
 */
#include <string.h>

#include "esp_log.h"
#include "ui_priv.h"

static const char *TAG = "ui_network";

#define POLL_MS     500
#define ROW_H       36

static lv_obj_t *s_status_label;
static lv_obj_t *s_scan_btn;
static lv_obj_t *s_forget_btn;
static lv_obj_t *s_list;

static gizmo_wifi_ap_t s_aps[GIZMO_WIFI_MAX_APS];
static int s_ap_count;
static uint32_t s_seen_generation;
static gizmo_wifi_state_t s_seen_state = GIZMO_WIFI_OFF;
static char s_seen_ssid[33];
static bool s_scanned_once;

/* Password dialog */
static lv_obj_t *s_dlg;
static lv_obj_t *s_dlg_ta;
static lv_group_t *s_dlg_group;
static char s_dlg_ssid[33];

/* ---------- helpers ---------- */

static lv_obj_t *small_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, ROW_H, ROW_H);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_center(lbl);
    return btn;
}

static lv_color_t signal_color(int rssi)
{
    if (rssi >= -60) return UI_COLOR_TEXT;
    if (rssi >= -75) return UI_COLOR_SUBTEXT;
    return lv_color_darken(UI_COLOR_SUBTEXT, LV_OPA_40);
}

/* ---------- password dialog ---------- */

static void dialog_close(void)
{
    if (!s_dlg) {
        return;
    }
    /* We're inside the keyboard's own event (and possibly the encoder's
     * processing of it): defer the delete, and keep the dialog group alive.
     * The keyboard leaves the group by itself when it is deleted. */
    lv_obj_delete_async(s_dlg);
    s_dlg = NULL;
    s_dlg_ta = NULL;
    ui_use_group(ui_settings_group());
}

static void keyboard_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        char pw[65];
        strlcpy(pw, lv_textarea_get_text(s_dlg_ta), sizeof(pw));
        dialog_close();
        gizmo_core_wifi_connect(s_dlg_ssid, pw);
        memset(pw, 0, sizeof(pw));
    } else if (code == LV_EVENT_CANCEL) {
        dialog_close();
    }
}

static void show_password_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    bool hidden = lv_textarea_get_password_mode(s_dlg_ta);
    lv_textarea_set_password_mode(s_dlg_ta, !hidden);
    lv_label_set_text(lv_obj_get_child(btn, 0), hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void dialog_open(const char *ssid)
{
    strlcpy(s_dlg_ssid, ssid, sizeof(s_dlg_ssid));

    /* Full-screen, clickable overlay so touches don't reach the screen below. */
    s_dlg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_dlg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_dlg, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_dlg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_dlg, 0, 0);
    lv_obj_set_style_radius(s_dlg, 0, 0);
    lv_obj_set_style_pad_all(s_dlg, 6, 0);
    lv_obj_set_style_text_color(s_dlg, UI_COLOR_TEXT, 0);
    lv_obj_remove_flag(s_dlg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_dlg);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, LV_PCT(80));
    lv_label_set_text_fmt(title, LV_SYMBOL_WIFI "  %s", ssid);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 6);

    lv_obj_t *eye = lv_button_create(s_dlg);
    lv_obj_set_size(eye, 40, 28);
    lv_obj_align(eye, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(eye, UI_COLOR_CARD, 0);
    lv_obj_set_style_shadow_width(eye, 0, 0);
    lv_obj_add_event_cb(eye, show_password_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *eye_lbl = lv_label_create(eye);
    lv_label_set_text(eye_lbl, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(eye_lbl);

    s_dlg_ta = lv_textarea_create(s_dlg);
    lv_textarea_set_one_line(s_dlg_ta, true);
    lv_textarea_set_password_mode(s_dlg_ta, true);
    lv_textarea_set_max_length(s_dlg_ta, 64);
    lv_textarea_set_placeholder_text(s_dlg_ta, "Password");
    lv_obj_set_width(s_dlg_ta, LV_PCT(100));
    lv_obj_align(s_dlg_ta, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_add_state(s_dlg_ta, LV_STATE_FOCUSED);   /* show the cursor */

    lv_obj_t *kb = lv_keyboard_create(s_dlg);
    lv_obj_set_size(kb, LV_PCT(100), 150);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_dlg_ta);
    lv_obj_add_event_cb(kb, keyboard_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, keyboard_cb, LV_EVENT_CANCEL, NULL);

    /* Encoder: rotate over keys, push to type (edit mode from the start). */
    if (!s_dlg_group) {
        s_dlg_group = lv_group_create();
    }
    ui_add_focusable(s_dlg_group, kb);
    ui_use_group(s_dlg_group);
    lv_group_set_editing(s_dlg_group, true);
}

/* ---------- list ---------- */

static void network_clicked_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_ap_count) {
        return;
    }
    const gizmo_wifi_ap_t *ap = &s_aps[idx];

    gizmo_wifi_status_t st;
    gizmo_core_wifi_get_status(&st);
    if (st.state == GIZMO_WIFI_CONNECTED && strcmp(st.ssid, ap->ssid) == 0) {
        return;   /* already on it */
    }

    if (ap->secure) {
        dialog_open(ap->ssid);
    } else {
        gizmo_core_wifi_connect(ap->ssid, NULL);
    }
}

static void list_placeholder(const char *text)
{
    lv_obj_t *lbl = lv_label_create(s_list);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_SUBTEXT, 0);
    lv_obj_set_style_pad_all(lbl, 8, 0);
}

static void list_rebuild(const gizmo_wifi_status_t *st)
{
    lv_obj_clean(s_list);
    s_ap_count = gizmo_core_wifi_get_scan_results(s_aps, GIZMO_WIFI_MAX_APS);

    if (s_ap_count == 0) {
        list_placeholder(st->scanning ? "Scanning..." :
                         s_scanned_once ? "No networks found" : "Tap " LV_SYMBOL_REFRESH " to scan");
    }

    for (int i = 0; i < s_ap_count; i++) {
        const gizmo_wifi_ap_t *ap = &s_aps[i];
        lv_obj_t *btn = lv_list_add_button(s_list, LV_SYMBOL_WIFI, ap->ssid);
        lv_obj_set_style_bg_color(btn, UI_COLOR_CARD, 0);
        lv_obj_set_style_text_color(btn, UI_COLOR_TEXT, 0);
        lv_obj_add_event_cb(btn, network_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        /* Signal strength via icon colour. */
        lv_obj_t *icon = lv_obj_get_child(btn, 0);
        lv_obj_set_style_text_color(icon, signal_color(ap->rssi), 0);

        bool current = st->state == GIZMO_WIFI_CONNECTED && strcmp(st->ssid, ap->ssid) == 0;
        lv_obj_t *tag = lv_label_create(btn);
        lv_label_set_text(tag, current ? LV_SYMBOL_OK : (ap->secure ? "secured" : "open"));
        lv_obj_set_style_text_color(tag, current ? UI_COLOR_OK : UI_COLOR_SUBTEXT, 0);
    }

    ui_settings_refresh_group();
}

/* ---------- status ---------- */

static void status_update(const gizmo_wifi_status_t *st)
{
    switch (st->state) {
    case GIZMO_WIFI_CONNECTED:
        lv_label_set_text_fmt(s_status_label, LV_SYMBOL_OK " %s\n%s", st->ssid, st->ip);
        lv_obj_set_style_text_color(s_status_label, UI_COLOR_OK, 0);
        break;
    case GIZMO_WIFI_CONNECTING:
        lv_label_set_text_fmt(s_status_label, "Connecting to\n%s...", st->ssid);
        lv_obj_set_style_text_color(s_status_label, UI_COLOR_TEXT, 0);
        break;
    case GIZMO_WIFI_FAILED:
        lv_label_set_text_fmt(s_status_label, "%s\n%s", st->ssid,
                              st->fail_reason ? st->fail_reason : "Connection failed");
        lv_obj_set_style_text_color(s_status_label, UI_COLOR_LOW, 0);
        break;
    case GIZMO_WIFI_OFF:
        lv_label_set_text(s_status_label, "Wi-Fi unavailable");
        lv_obj_set_style_text_color(s_status_label, UI_COLOR_LOW, 0);
        break;
    default:
        lv_label_set_text(s_status_label, "Not connected");
        lv_obj_set_style_text_color(s_status_label, UI_COLOR_SUBTEXT, 0);
        break;
    }

    if (st->scanning) {
        lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(s_scan_btn, LV_STATE_DISABLED);
    }

    bool want_forget = st->ssid[0] && st->state != GIZMO_WIFI_DISCONNECTED;
    bool has_forget = !lv_obj_has_flag(s_forget_btn, LV_OBJ_FLAG_HIDDEN);
    if (want_forget != has_forget) {
        if (want_forget) {
            lv_obj_remove_flag(s_forget_btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_forget_btn, LV_OBJ_FLAG_HIDDEN);
        }
        ui_settings_refresh_group();
    }
}

static void poll_cb(lv_timer_t *t)
{
    (void)t;
    gizmo_wifi_status_t st;
    gizmo_core_wifi_get_status(&st);
    status_update(&st);

    /* Rebuild the list on new results, or when the connected network changes
     * (to move the check mark). */
    bool changed = st.scan_generation != s_seen_generation ||
                   st.state != s_seen_state || strcmp(st.ssid, s_seen_ssid) != 0;
    if (changed && !s_dlg) {
        if (st.scan_generation != s_seen_generation) {
            s_scanned_once = true;
        }
        s_seen_generation = st.scan_generation;
        s_seen_state = st.state;
        strlcpy(s_seen_ssid, st.ssid, sizeof(s_seen_ssid));
        list_rebuild(&st);
    }
}

static void scan_cb(lv_event_t *e)
{
    (void)e;
    if (gizmo_core_wifi_scan() == ESP_OK && s_ap_count == 0) {
        lv_obj_clean(s_list);
        list_placeholder("Scanning...");
    }
}

static void forget_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Forget network");
    gizmo_core_wifi_forget();
}

/* ---------- public ---------- */

void ui_network_tab_shown(void)
{
    /* Scan on first visit so the list isn't empty. */
    if (!s_scanned_once) {
        scan_cb(NULL);
    }
}

void ui_network_tab_create(lv_obj_t *page)
{
    lv_obj_set_style_pad_all(page, 8, 0);
    lv_obj_set_style_pad_row(page, 6, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    /* Status row */
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_set_size(row, LV_PCT(100), ROW_H);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_status_label = lv_label_create(row);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(s_status_label, 1);
    lv_obj_set_height(s_status_label, ROW_H);
    lv_label_set_text(s_status_label, "");

    s_scan_btn = small_button(row, LV_SYMBOL_REFRESH, scan_cb);
    s_forget_btn = small_button(row, LV_SYMBOL_CLOSE, forget_cb);
    lv_obj_add_flag(s_forget_btn, LV_OBJ_FLAG_HIDDEN);

    /* Network list */
    s_list = lv_list_create(page);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 4, 0);

    gizmo_wifi_status_t st;
    gizmo_core_wifi_get_status(&st);
    status_update(&st);
    list_rebuild(&st);

    lv_timer_create(poll_cb, POLL_MS, NULL);
}
