/*
 * IANA timezone name -> POSIX TZ rule.
 *
 * The device has no tz database, so we ship a curated table covering every
 * commonly used UTC offset. Rules follow tzdata as of late 2026 (checked
 * against the host zoneinfo for 2026-2027). Ordered by
 * standard UTC offset so the settings dropdown reads west to east.
 *
 * Angle-bracket names (e.g. "<+08>-8") are supported by picolibc's tzset.
 * Remember POSIX inverts the sign: "-8" means UTC+8.
 */
#include "timezones.h"

#include <string.h>

const gizmo_tz_t g_timezones[] = {
    { "Pacific/Pago_Pago",              -660, "SST11" },
    { "Pacific/Honolulu",               -600, "HST10" },
    { "America/Anchorage",              -540, "AKST9AKDT,M3.2.0,M11.1.0" },
    { "America/Los_Angeles",            -480, "PST8PDT,M3.2.0,M11.1.0" },
    { "America/Denver",                 -420, "MST7MDT,M3.2.0,M11.1.0" },
    { "America/Vancouver",              -420, "MST7" },             /* permanent UTC-7 since 2026-11-01 */
    { "America/Phoenix",                -420, "MST7" },
    { "America/Chicago",                -360, "CST6CDT,M3.2.0,M11.1.0" },
    { "America/Mexico_City",            -360, "CST6" },
    { "America/New_York",               -300, "EST5EDT,M3.2.0,M11.1.0" },
    { "America/Toronto",                -300, "EST5EDT,M3.2.0,M11.1.0" },
    { "America/Bogota",                 -300, "<-05>5" },
    { "America/Lima",                   -300, "<-05>5" },
    { "America/Halifax",                -240, "AST4ADT,M3.2.0,M11.1.0" },
    { "America/Caracas",                -240, "<-04>4" },
    { "America/Santiago",               -240, "<-04>4<-03>,M9.1.6/24,M4.1.6/24" },
    { "America/St_Johns",               -210, "NST3:30NDT,M3.2.0,M11.1.0" },
    { "America/Sao_Paulo",              -180, "<-03>3" },
    { "America/Buenos_Aires",           -180, "<-03>3" },
    { "Atlantic/South_Georgia",         -120, "<-02>2" },
    { "Atlantic/Azores",                 -60, "<-01>1<+00>,M3.5.0/0,M10.5.0/1" },
    { "Etc/UTC",                           0, "UTC0" },
    { "Europe/London",                     0, "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Europe/Dublin",                     0, "IST-1GMT0,M10.5.0,M3.5.0/1" },
    { "Europe/Lisbon",                     0, "WET0WEST,M3.5.0/1,M10.5.0" },
    { "Africa/Accra",                      0, "GMT0" },
    { "Europe/Paris",                     60, "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Berlin",                    60, "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Amsterdam",                 60, "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Madrid",                    60, "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Rome",                      60, "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Stockholm",                 60, "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Warsaw",                    60, "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Zurich",                    60, "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Africa/Lagos",                     60, "WAT-1" },
    { "Europe/Athens",                   120, "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Helsinki",                 120, "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Kyiv",                     120, "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Asia/Jerusalem",                  120, "IST-2IDT,M3.4.4/26,M10.5.0" },
    { "Africa/Cairo",                    120, "EET-2EEST,M4.5.5/0,M10.5.4/24" },
    { "Africa/Johannesburg",             120, "SAST-2" },
    { "Europe/Istanbul",                 180, "<+03>-3" },
    { "Europe/Moscow",                   180, "MSK-3" },
    { "Asia/Riyadh",                     180, "<+03>-3" },
    { "Africa/Nairobi",                  180, "EAT-3" },
    { "Asia/Tehran",                     210, "<+0330>-3:30" },
    { "Asia/Dubai",                      240, "<+04>-4" },
    { "Asia/Kabul",                      270, "<+0430>-4:30" },
    { "Asia/Karachi",                    300, "PKT-5" },
    { "Asia/Tashkent",                   300, "<+05>-5" },
    { "Asia/Kolkata",                    330, "IST-5:30" },
    { "Asia/Kathmandu",                  345, "<+0545>-5:45" },
    { "Asia/Dhaka",                      360, "<+06>-6" },
    { "Asia/Yangon",                     390, "<+0630>-6:30" },
    { "Asia/Bangkok",                    420, "<+07>-7" },
    { "Asia/Ho_Chi_Minh",                420, "<+07>-7" },
    { "Asia/Jakarta",                    420, "WIB-7" },
    { "Asia/Singapore",                  480, "<+08>-8" },
    { "Asia/Kuala_Lumpur",               480, "<+08>-8" },
    { "Asia/Makassar",                   480, "WITA-8" },
    { "Asia/Manila",                     480, "PST-8" },
    { "Asia/Hong_Kong",                  480, "HKT-8" },
    { "Asia/Shanghai",                   480, "CST-8" },
    { "Asia/Taipei",                     480, "CST-8" },
    { "Australia/Perth",                 480, "AWST-8" },
    { "Asia/Seoul",                      540, "KST-9" },
    { "Asia/Tokyo",                      540, "JST-9" },
    { "Asia/Jayapura",                   540, "WIT-9" },
    { "Australia/Darwin",                570, "ACST-9:30" },
    { "Australia/Adelaide",              570, "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
    { "Australia/Brisbane",              600, "AEST-10" },
    { "Australia/Sydney",                600, "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Australia/Melbourne",             600, "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { "Pacific/Noumea",                  660, "<+11>-11" },
    { "Pacific/Auckland",                720, "NZST-12NZDT,M9.5.0,M4.1.0/3" },
    { "Pacific/Fiji",                    720, "<+12>-12" },
    { "Pacific/Tongatapu",               780, "<+13>-13" },
    { "Pacific/Kiritimati",              840, "<+14>-14" },
};

const int g_timezone_count = sizeof(g_timezones) / sizeof(g_timezones[0]);

int timezone_find(const char *name)
{
    for (int i = 0; name && i < g_timezone_count; i++) {
        if (strcmp(g_timezones[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}
