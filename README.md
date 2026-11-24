# cores3_gizmo

M5Stack Core S3 Gizmo: an LVGL home screen with a settings page for display
brightness, controllable by touch or by the M5Stack Module HMI encoder.

## Hardware

- M5Stack CoreS3 (ESP32-S3, 16 MB flash, 8 MB PSRAM)
- Module HMI (optional, I2C 0x41 on the internal bus, GPIO12/11)
- Bottom3 base

## Layout

| Path | Role |
| --- | --- |
| `components/gizmo_core` | Board layer: display, backlight, NVS settings, Module HMI encoder |
| `components/gizmo_ui` | LVGL screens (home, settings); uses only `gizmo_core.h` |
| `main` | `app_main`: init core, build UI |

## Controls

| Input | Action |
| --- | --- |
| Touch | Tap icons and buttons, drag the slider |
| Encoder rotate | Move focus; changes the value while editing the slider |
| Encoder push | Activate the focused item; on the slider, enter or leave edit mode |
| HMI button 1 | Back |
| HMI button 2 | Same as encoder push |

If one encoder click moves more than one step, set
`Gizmo core → Module HMI encoder counts per detent` in `make menuconfig`.

## Build

Requires ESP-IDF v6.1. The Makefile activates it per command.

```sh
make            # build
make run        # flash + monitor (PORT=/dev/ttyACM0)
make menuconfig
make docs       # Doxygen HTML in docs/html/index.html
make help
```

Pass `IDF_ACTIVATE=/path/to/activate_idf_vX.sh` if ESP-IDF is installed elsewhere.
