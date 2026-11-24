/*
 * Wi-Fi station: scan, connect, reconnect, forget.
 *
 * All driver events land in wifi_event_cb (default event loop task) and update
 * a small state block guarded by a mutex. The UI only reads that state through
 * wifi_get_status() / wifi_get_scan_results(), so it never blocks on Wi-Fi.
 *
 * Reconnect policy:
 *   - a fresh connect() gets CONNECT_RETRIES quick attempts, then FAILED
 *     (with a reason such as "Wrong password");
 *   - once a network has worked, a dropped link retries every
 *     RECONNECT_DELAY_MS forever (router reboot, out of range, ...).
 */
#include "wifi.h"
#include "timesync.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "wifi";

#define HOSTNAME            "cores3-gizmo"
#define CONNECT_RETRIES     3
#define RECONNECT_DELAY_MS  10000

static SemaphoreHandle_t s_mutex;
static esp_netif_t *s_netif;
static esp_timer_handle_t s_reconnect_timer;

static gizmo_wifi_status_t s_status;
static gizmo_wifi_ap_t s_aps[GIZMO_WIFI_MAX_APS];
static int s_ap_count;

static int s_retries;
static bool s_ever_connected;   /* current network has connected at least once */
static bool s_stopping;         /* user asked to disconnect/forget */

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

static const char *reason_text(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return "Wrong password";
    case WIFI_REASON_NO_AP_FOUND:
        return "Network not found";
    default:
        return "Connection failed";
    }
}

static void set_state(gizmo_wifi_state_t st, const char *fail_reason)
{
    LOCK();
    s_status.state = st;
    s_status.fail_reason = fail_reason;
    if (st != GIZMO_WIFI_CONNECTED) {
        s_status.ip[0] = '\0';
    }
    UNLOCK();
}

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (!s_stopping) {
        esp_wifi_connect();
    }
}

static void handle_scan_done(void)
{
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);

    wifi_ap_record_t *recs = n ? calloc(n, sizeof(*recs)) : NULL;
    if (n && !recs) {
        esp_wifi_clear_ap_list();
        n = 0;
    }
    if (recs) {
        esp_wifi_scan_get_ap_records(&n, recs);
    }

    LOCK();
    s_ap_count = 0;
    /* Records come sorted by RSSI; keep the strongest entry per SSID. */
    for (int i = 0; i < n && s_ap_count < GIZMO_WIFI_MAX_APS; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (ssid[0] == '\0') {
            continue;   /* hidden network */
        }
        bool dup = false;
        for (int j = 0; j < s_ap_count && !dup; j++) {
            dup = strcmp(s_aps[j].ssid, ssid) == 0;
        }
        if (dup) {
            continue;
        }
        gizmo_wifi_ap_t *ap = &s_aps[s_ap_count++];
        strlcpy(ap->ssid, ssid, sizeof(ap->ssid));
        ap->rssi = recs[i].rssi;
        ap->secure = recs[i].authmode != WIFI_AUTH_OPEN;
    }
    s_status.scanning = false;
    s_status.scan_generation++;
    UNLOCK();

    free(recs);
    ESP_LOGI(TAG, "Scan done: %d networks", s_ap_count);
}

static void wifi_event_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        wifi_config_t cfg = { 0 };
        esp_wifi_get_config(WIFI_IF_STA, &cfg);
        if (cfg.sta.ssid[0]) {
            ESP_LOGI(TAG, "Reconnecting to saved network \"%s\"", cfg.sta.ssid);
            LOCK();
            strlcpy(s_status.ssid, (const char *)cfg.sta.ssid, sizeof(s_status.ssid));
            UNLOCK();
            /* It worked before, so keep retrying rather than reporting failure. */
            s_ever_connected = true;
            set_state(GIZMO_WIFI_CONNECTING, NULL);
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = data;
        ESP_LOGI(TAG, "Disconnected, reason %d", ev->reason);

        if (s_stopping) {
            set_state(GIZMO_WIFI_DISCONNECTED, NULL);
            return;
        }
        /* Our own disconnect() while switching networks: the new connect is already under way. */
        if (ev->reason == WIFI_REASON_ASSOC_LEAVE && s_status.state == GIZMO_WIFI_CONNECTING) {
            return;
        }

        if (s_retries < CONNECT_RETRIES) {
            s_retries++;
            set_state(GIZMO_WIFI_CONNECTING, NULL);
            esp_wifi_connect();
        } else if (s_ever_connected) {
            set_state(GIZMO_WIFI_CONNECTING, NULL);
            esp_timer_start_once(s_reconnect_timer, RECONNECT_DELAY_MS * 1000ULL);
        } else {
            /* Never worked: don't keep bad credentials for the next boot. */
            wifi_config_t empty = { 0 };
            esp_wifi_set_config(WIFI_IF_STA, &empty);
            set_state(GIZMO_WIFI_FAILED, reason_text(ev->reason));
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        handle_scan_done();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        s_retries = 0;
        s_ever_connected = true;
        LOCK();
        s_status.state = GIZMO_WIFI_CONNECTED;
        s_status.fail_reason = NULL;
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&ev->ip_info.ip));
        UNLOCK();
        ESP_LOGI(TAG, "Connected, IP %s", s_status.ip);
        timesync_on_connected();
    }
}

esp_err_t wifi_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mutex, ESP_ERR_NO_MEM, TAG, "mutex");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    esp_err_t err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "event loop");

    s_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_netif, ESP_FAIL, TAG, "create netif");
    esp_netif_set_hostname(s_netif, HOSTNAME);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    /* Flash storage (the default) keeps credentials across reboots. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_FLASH), TAG, "storage");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_cb, NULL),
                        TAG, "wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_cb, NULL),
                        TAG, "ip handler");

    const esp_timer_create_args_t targs = { .callback = reconnect_timer_cb, .name = "wifi_rc" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_reconnect_timer), TAG, "timer");

    /* Initialised; STA_START moves this to CONNECTING if a network is saved. */
    s_status.state = GIZMO_WIFI_DISCONNECTED;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    return ESP_OK;
}

esp_err_t wifi_scan_start(void)
{
    LOCK();
    bool busy = s_status.scanning;
    UNLOCK();
    if (busy) {
        return ESP_OK;
    }

    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err == ESP_OK) {
        LOCK();
        s_status.scanning = true;
        UNLOCK();
    } else {
        ESP_LOGW(TAG, "Scan start failed: %s", esp_err_to_name(err));
    }
    return err;
}

int wifi_get_scan_results(gizmo_wifi_ap_t *out, int max)
{
    LOCK();
    int n = s_ap_count < max ? s_ap_count : max;
    memcpy(out, s_aps, n * sizeof(*out));
    UNLOCK();
    return n;
}

esp_err_t wifi_connect(const char *ssid, const char *password)
{
    ESP_RETURN_ON_FALSE(ssid && ssid[0], ESP_ERR_INVALID_ARG, TAG, "empty ssid");

    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    if (password) {
        strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    }
    /* Accept WPA2/WPA3 personal and open networks. */
    cfg.sta.threshold.authmode = (password && password[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;

    esp_timer_stop(s_reconnect_timer);
    s_stopping = false;
    s_retries = 0;
    s_ever_connected = false;

    LOCK();
    strlcpy(s_status.ssid, ssid, sizeof(s_status.ssid));
    UNLOCK();
    set_state(GIZMO_WIFI_CONNECTING, NULL);

    esp_wifi_disconnect();
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "set config");
    ESP_LOGI(TAG, "Connecting to \"%s\"", ssid);
    return esp_wifi_connect();
}

esp_err_t wifi_forget(void)
{
    s_stopping = true;
    esp_timer_stop(s_reconnect_timer);
    esp_wifi_disconnect();

    /* Clear stored credentials so we don't reconnect at boot. */
    wifi_config_t cfg = { 0 };
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);

    LOCK();
    s_status.ssid[0] = '\0';
    UNLOCK();
    set_state(GIZMO_WIFI_DISCONNECTED, NULL);
    ESP_LOGI(TAG, "Network forgotten");
    return err;
}

void wifi_get_status(gizmo_wifi_status_t *out)
{
    if (!s_mutex) {
        memset(out, 0, sizeof(*out));
        out->state = GIZMO_WIFI_OFF;
        return;
    }
    LOCK();
    *out = s_status;
    UNLOCK();

    if (out->state == GIZMO_WIFI_CONNECTED) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            out->rssi = ap.rssi;
        }
    }
}
