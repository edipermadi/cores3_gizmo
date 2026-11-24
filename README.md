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
| `components/gizmo_ui` | LVGL screens; uses only `gizmo_core.h`. One file per screen/tab: `ui_home.c`, `ui_settings.c`, `ui_display.c`, `ui_network.c`, `ui_diag.c`, `ui_spectrum.c`, shared bits in `ui_common.c` |
| `components/gizmo_console` | Serial command console (screenshot, simulated touch) |
| `main` | `app_main`: init core, build UI, start console |

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
Pick your timezone in Settings → Network (e.g. `Asia/Singapore`); it is saved
and applied immediately. The pre-selection before any choice is
`Gizmo core → Default timezone` in `make menuconfig` (default `Etc/UTC`).

The Spectrum page shows the on-board microphones live (ES7210, 16 kHz,
1024-point FFT via esp-dsp, ~31 frames/s, 60 Hz-8 kHz log scale). Tap the
plot to switch between **bars** (32 bands with peak hold) and a **waterfall**
spectrogram (~4.5 s of history). While the page is open the Bottom3 base's
LEDs are a stereo VU meter (left bar = left mic, right bar = right mic,
green/yellow/red, filling bottom-up). The mics and LEDs only run while the
page is open.

The Diagnostics page has an **I2C** tab (devices on the internal bus) and a
**Power** tab (AXP2101: power source, battery %, charge phase, battery/USB/
system voltages, PMU temperature, raw status registers).

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

## Serial console

A linenoise console (`gizmo>`) runs on the USB port; use it from
`make monitor` or script it with `tools/gizmo.py`:

```sh
make screenshot                  # -> screenshot.png (OUT=file.png)
make touch X=60 Y=98             # tap at 60,98 (MS=600 for a long press)
make swipe X1=145 Y1=150 X2=260 Y2=150   # drag (MS=300 default)
python tools/gizmo.py cmd help   # any console command
```

| Command | Effect |
| --- | --- |
| `screenshot` | Active screen as RGB565, zlib-compressed (ROM deflate), base64 between `SCREENSHOT BEGIN/END` lines (~3 KB per frame) |
| `touch X Y [MS]` | Tap at X,Y held for MS ms (default 100), via a virtual LVGL pointer |
| `swipe X1 Y1 X2 Y2 [MS]` | Press at X1,Y1, move linearly to X2,Y2 over MS ms (default 300), release |
| `i2cscan` | List devices answering on the internal I2C bus |
| `led IDX\|all\|off [R G B]` | Light base RGB LEDs (a single index lights only that LED) |

