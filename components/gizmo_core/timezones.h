#pragma once

/**
 * @file
 * @brief Built-in IANA timezone table (private to gizmo_core).
 */

/** One selectable timezone. */
typedef struct {
    const char *name;        /**< IANA name, e.g. "Asia/Singapore". */
    int std_offset_min;      /**< Standard (non-DST) offset from UTC, minutes. */
    const char *posix;       /**< POSIX TZ rule for setenv("TZ"). */
} gizmo_tz_t;

extern const gizmo_tz_t g_timezones[];
extern const int g_timezone_count;

/** Index of an IANA name in g_timezones, or -1. */
int timezone_find(const char *name);
