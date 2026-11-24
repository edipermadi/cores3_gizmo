# cores3_gizmo

M5Stack Core S3 Gizmo: an LVGL home screen with a tabbed settings page
(display brightness, Wi-Fi), a status bar (Wi-Fi, battery, charging), and
navigation by touch or by the M5Stack Module HMI encoder.

## Hardware

- M5Stack CoreS3 (ESP32-S3, 16 MB flash, 8 MB PSRAM)
- Module HMI (optional, I2C 0x41 on the internal bus, GPIO12/11)
- Bottom3 base

## Layout

| Path | Role |
| --- | --- |
| `components/gizmo_core` | Board layer: display, backlight, NVS settings, battery/charging (AXP2101), Wi-Fi station, Module HMI encoder |
| `components/gizmo_ui` | LVGL screens; uses only `gizmo_core.h`. One file per screen/tab: `ui_home.c`, `ui_settings.c`, `ui_display.c`, `ui_network.c`, shared bits in `ui_common.c` |
| `main` | `app_main`: init core, build UI |

## Controls

| Input | Action |
| --- | --- |
| Touch | Tap icons and buttons, drag the slider |
| Encoder rotate | Move focus; changes the value while editing the slider |
| Encoder push | Activate the focused item; on the slider, enter or leave edit mode |
| HMI button 1 | Back |
| HMI button 2 | Same as encoder push |

On the Network tab, pick a network to connect; secured networks ask for a
password on an on-screen keyboard (encoder: rotate over keys, push to type).
The last network that connected is remembered and rejoined at boot;
the ✕ button forgets it.

The home top bar shows a 24h clock. At boot the time comes from the
battery-backed BM8563 RTC; once Wi-Fi connects it is synced over NTP and the
RTC is updated. `--:--` means neither source has a valid time yet.
Pick your timezone in Settings → Time (e.g. `Asia/Singapore`); it is saved
and applied immediately. The pre-selection before any choice is
`Gizmo core → Default timezone` in `make menuconfig` (default `Etc/UTC`).

If one encoder click moves more than one step, set
`Gizmo core → Module HMI encoder counts per detent` in `make menuconfig`.

## Build

Requires ESP-IDF v6.1. The Makefile activates it per command.

```sh
make            # build
make run        # flash + monitor (port auto-detected, or PORT=/dev/ttyACM1)
make menuconfig
make docs       # Doxygen HTML in docs/html/index.html
make help
```

Pass `IDF_ACTIVATE=/path/to/activate_idf_vX.sh` if ESP-IDF is installed elsewhere.
