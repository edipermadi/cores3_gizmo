/*
 * Microphone spectrum analyser.
 *
 * Capture: ES7210 ADC (two on-board mics) via the BSP / esp_codec_dev,
 *          16 kHz, 16-bit, 2 channels, averaged to mono.
 * Analysis: 1024-point FFT (esp-dsp, SIMD on ESP32-S3) every HOP samples
 *          (50% overlap -> ~31 spectra/s), Hann window. The 512 bins
 *          (15.6 Hz each) are grouped into GIZMO_SPECTRUM_BANDS log-spaced
 *          bands from BAND_F_MIN to Nyquist; each band reports its strongest
 *          bin in dBFS, mapped to 0..1 over DB_FLOOR..DB_CEIL.
 *
 * A finer GIZMO_SPECTRUM_FINE-column log spectrum (same range and scaling,
 * 0..255) feeds the waterfall view; `seq` counts frames so a consumer can add
 * exactly one waterfall row per FFT frame.
 *
 * VU meter: each block's per-channel sample peak drives a 5-LED bar per side
 * on the Bottom3 base (left mic -> left bar, right mic -> right bar, filling
 * bottom-up; see s_vu_bar). Instant attack, VU_RELEASE_DB per frame release. LEDs are
 * switched off when the analyser stops.
 *
 * Runs only between gizmo_core_spectrum_start() and _stop(), so the mics and
 * CPU are idle unless the spectrum page is open. Results are cached for the
 * UI, which never blocks on audio.
 */
#include "spectrum.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "dsps_fft2r.h"
#include "dsps_wind_hann.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "leds.h"
#include "sdkconfig.h"

static const char *TAG = "spectrum";

#define SAMPLE_RATE   16000
#define CHANNELS      2
#define FFT_N         1024
#define HOP           (FFT_N / 2)
#define BAND_F_MIN    60.0f
#define DB_FLOOR      (-78.0f)   /* ~room noise at 30 dB gain shows as short bars */
#define DB_CEIL       (-20.0f)
#define MIC_GAIN_DB   30.0f
#define TASK_CORE     0

#define VU_LEDS_PER_SIDE 5
#define VU_DB_MIN       (-54.0f)
#define VU_DB_MAX       (-6.0f)
#define VU_RELEASE_DB   1.0f     /* per frame (~31 dB/s) */

static esp_codec_dev_handle_t s_mic;
static float s_vu_db[2] = { -120.0f, -120.0f };
static TaskHandle_t s_task;
static volatile bool s_run;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static gizmo_spectrum_t s_out;

/* Bin range [lo, hi) for each band / fine column, computed once. */
static uint16_t s_band_lo[GIZMO_SPECTRUM_BANDS];
static uint16_t s_band_hi[GIZMO_SPECTRUM_BANDS];
static uint16_t s_fine_lo[GIZMO_SPECTRUM_FINE];
static uint16_t s_fine_hi[GIZMO_SPECTRUM_FINE];

/**
 * Split BAND_F_MIN..Nyquist into n log-spaced ranges of FFT bins.
 * With `distinct`, every range gets at least one bin of its own (bars);
 * otherwise neighbouring low-frequency columns may share a bin (waterfall).
 */
static void compute_ranges(uint16_t *lo, uint16_t *hi, int n, bool distinct)
{
    const float f_max = SAMPLE_RATE / 2.0f;
    const float bin_hz = (float)SAMPLE_RATE / FFT_N;
    int prev = 1;
    for (int b = 0; b < n; b++) {
        float f_lo = BAND_F_MIN * powf(f_max / BAND_F_MIN, (float)b / n);
        float f_hi = BAND_F_MIN * powf(f_max / BAND_F_MIN, (float)(b + 1) / n);
        int l = distinct ? prev : (int)floorf(f_lo / bin_hz);
        int h = (int)ceilf(f_hi / bin_hz);
        if (l < 1) l = 1;
        if (h <= l) h = l + 1;
        if (h > FFT_N / 2) h = FFT_N / 2;
        if (l >= h) l = h - 1;
        lo[b] = l;
        hi[b] = h;
        prev = h;
    }
}

static void compute_bands(void)
{
    compute_ranges(s_band_lo, s_band_hi, GIZMO_SPECTRUM_BANDS, true);
    compute_ranges(s_fine_lo, s_fine_hi, GIZMO_SPECTRUM_FINE, false);
}

static uint32_t s_seq;

/** dBFS of a bin power, given the window's full-scale amplitude. */
static inline float bin_db(float pow, float full_scale)
{
    return pow > 0 ? 10.0f * log10f(pow) - 20.0f * log10f(full_scale) : -200.0f;
}

static inline float db_to_unit(float db)
{
    float v = (db - DB_FLOOR) / (DB_CEIL - DB_FLOOR);
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

static inline float to_dbfs(float a)
{
    return a > 0 ? 20.0f * log10f(a) : -120.0f;
}

/*
 * Bottom3 LED chain (verified on hardware): it runs down the right bar and up
 * the left bar, so listed bottom -> top per bar:
 *   left  bar: 5 6 7 8 9      right bar: 4 3 2 1 0
 */
static const int s_vu_bar[2][VU_LEDS_PER_SIDE] = {
    { 5, 6, 7, 8, 9 },   /* left  */
    { 4, 3, 2, 1, 0 },   /* right */
};

/** Draw one side's bar bottom-up: `level` in LEDs (0..5), top LED dimmed. */
static void vu_draw_side(int side, float level)
{
    static const uint8_t colors[VU_LEDS_PER_SIDE][3] = {
        { 0, 255, 0 }, { 0, 255, 0 }, { 0, 255, 0 }, { 255, 170, 0 }, { 255, 0, 0 },
    };
    for (int j = 0; j < VU_LEDS_PER_SIDE; j++) {
        float fill = level - j;
        fill = fill < 0 ? 0 : (fill > 1 ? 1 : fill);
        float k = fill * CONFIG_GIZMO_LED_BRIGHTNESS / 255.0f;
        leds_set(s_vu_bar[side][j], (uint8_t)(colors[j][0] * k), (uint8_t)(colors[j][1] * k),
                 (uint8_t)(colors[j][2] * k));
    }
}

static void vu_update(float left_db, float right_db)
{
    const float in[2] = { left_db, right_db };
    for (int side = 0; side < 2; side++) {
        /* Instant attack, linear (in dB) release. */
        s_vu_db[side] = in[side] > s_vu_db[side] ? in[side] : fmaxf(in[side], s_vu_db[side] - VU_RELEASE_DB);
        float level = (s_vu_db[side] - VU_DB_MIN) / (VU_DB_MAX - VU_DB_MIN) * VU_LEDS_PER_SIDE;
        vu_draw_side(side, level);
    }
    leds_show();
}

static void vu_off(void)
{
    s_vu_db[0] = s_vu_db[1] = -120.0f;
    leds_clear();
    leds_show();
}

static void spectrum_task(void *arg)
{
    (void)arg;
    int16_t *pcm = heap_caps_malloc(HOP * CHANNELS * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    float *window = heap_caps_aligned_alloc(16, FFT_N * sizeof(float), MALLOC_CAP_INTERNAL);
    float *ring = heap_caps_calloc(FFT_N, sizeof(float), MALLOC_CAP_INTERNAL);  /* last N mono samples */
    float *fft = heap_caps_aligned_alloc(16, FFT_N * 2 * sizeof(float), MALLOC_CAP_INTERNAL);
    if (!pcm || !window || !ring || !fft) {
        ESP_LOGE(TAG, "out of memory");
        goto out;
    }
    dsps_wind_hann_f32(window, FFT_N);

    /* Amplitude of a full-scale sine after Hann window: N/2 * 0.5. */
    const float full_scale = FFT_N / 4.0f;
    const float bin_hz = (float)SAMPLE_RATE / FFT_N;

    while (s_run) {
        if (esp_codec_dev_read(s_mic, pcm, HOP * CHANNELS * sizeof(int16_t)) != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        /* Slide the analysis window by HOP and append the new mono samples. */
        memmove(ring, ring + HOP, (FFT_N - HOP) * sizeof(float));
        float peak_abs = 0;
        int ch_peak[2] = { 0, 0 };
        for (int i = 0; i < HOP; i++) {
            int a0 = abs(pcm[i * 2]), a1 = abs(pcm[i * 2 + 1]);
            if (a0 > ch_peak[0]) ch_peak[0] = a0;
            if (a1 > ch_peak[1]) ch_peak[1] = a1;
            float s = (pcm[i * 2] + pcm[i * 2 + 1]) * (0.5f / 32768.0f);
            ring[FFT_N - HOP + i] = s;
            float a = fabsf(s);
            if (a > peak_abs) {
                peak_abs = a;
            }
        }
#if CONFIG_GIZMO_VU_SWAP_MICS
        const int left_ch = 1, right_ch = 0;
#else
        const int left_ch = 0, right_ch = 1;
#endif
        float left_db = to_dbfs(ch_peak[left_ch] / 32768.0f);
        float right_db = to_dbfs(ch_peak[right_ch] / 32768.0f);
        vu_update(left_db, right_db);

        for (int i = 0; i < FFT_N; i++) {
            fft[i * 2] = ring[i] * window[i];
            fft[i * 2 + 1] = 0;
        }
        dsps_fft2r_fc32(fft, FFT_N);
        dsps_bit_rev_fc32(fft, FFT_N);

        gizmo_spectrum_t r = { 0 };
        float best_db = -200.0f;
        int best_bin = 0;
        for (int b = 0; b < GIZMO_SPECTRUM_BANDS; b++) {
            float band_pow = 0;
            for (int k = s_band_lo[b]; k < s_band_hi[b]; k++) {
                float re = fft[k * 2], im = fft[k * 2 + 1];
                float p = re * re + im * im;
                if (p > band_pow) {
                    band_pow = p;
                }
                if (p > 0) {
                    float db = 10.0f * log10f(p) - 20.0f * log10f(full_scale);
                    if (db > best_db) {
                        best_db = db;
                        best_bin = k;
                    }
                }
            }
            float db = band_pow > 0 ? 10.0f * log10f(band_pow) - 20.0f * log10f(full_scale) : DB_FLOOR;
            float v = (db - DB_FLOOR) / (DB_CEIL - DB_FLOOR);
            r.level[b] = v < 0 ? 0 : (v > 1 ? 1 : v);
        }
        for (int c = 0; c < GIZMO_SPECTRUM_FINE; c++) {
            float col_pow = 0;
            for (int k = s_fine_lo[c]; k < s_fine_hi[c]; k++) {
                float re = fft[k * 2], im = fft[k * 2 + 1];
                float p = re * re + im * im;
                if (p > col_pow) {
                    col_pow = p;
                }
            }
            r.fine[c] = (uint8_t)(db_to_unit(bin_db(col_pow, full_scale)) * 255.0f + 0.5f);
        }
        r.seq = ++s_seq;
        r.peak_hz = best_bin * bin_hz;
        r.peak_db = best_db;
        r.input_dbfs = peak_abs > 0 ? 20.0f * log10f(peak_abs) : -120.0f;
        r.left_dbfs = left_db;
        r.right_dbfs = right_db;
        r.valid = true;

        taskENTER_CRITICAL(&s_lock);
        s_out = r;
        taskEXIT_CRITICAL(&s_lock);
    }

out:
    vu_off();
    heap_caps_free(fft);
    heap_caps_free(ring);
    heap_caps_free(window);
    heap_caps_free(pcm);
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t spectrum_start(void)
{
    if (s_task) {
        return ESP_OK;
    }

    if (!s_mic) {
        s_mic = bsp_audio_codec_microphone_init();
        ESP_RETURN_ON_FALSE(s_mic, ESP_FAIL, TAG, "microphone init failed");
        ESP_RETURN_ON_ERROR(dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE), TAG, "fft init");
        compute_bands();
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = SAMPLE_RATE,
        .channel = CHANNELS,
        .bits_per_sample = 16,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_mic, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "mic open");
    esp_codec_dev_set_in_gain(s_mic, MIC_GAIN_DB);

    taskENTER_CRITICAL(&s_lock);
    memset(&s_out, 0, sizeof(s_out));
    taskEXIT_CRITICAL(&s_lock);

    s_run = true;
    if (xTaskCreatePinnedToCore(spectrum_task, "spectrum", 4096, NULL, 4, &s_task, TASK_CORE) != pdPASS) {
        s_run = false;
        esp_codec_dev_close(s_mic);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Started (%d Hz, %d-point FFT, %d bands)", SAMPLE_RATE, FFT_N, GIZMO_SPECTRUM_BANDS);
    return ESP_OK;
}

void spectrum_stop(void)
{
    if (!s_task) {
        return;
    }
    s_run = false;
    /* The task exits after its current read (~32 ms). */
    for (int i = 0; i < 50 && s_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    esp_codec_dev_close(s_mic);
    ESP_LOGI(TAG, "Stopped");
}

esp_err_t spectrum_get(gizmo_spectrum_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = s_out;
    taskEXIT_CRITICAL(&s_lock);
    return out->valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}
