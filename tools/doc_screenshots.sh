#!/usr/bin/env bash
# Capture documentation screenshots from a running device over the serial
# console (tools/gizmo.py). Drives the UI with simulated taps and swipes, so
# the device should be idle (it is brought back to the home screen first).
#
#   tools/doc_screenshots.sh [OUT_DIR] [--port /dev/ttyACMx]
#
# The Network tab is captured with the connected SSID + IP, the timezone and
# the nearby SSIDs blurred (on the host, before the PNG is written).
set -euo pipefail

OUT=${1:-assets/screenshots}
shift || true
G="python $(dirname "$0")/gizmo.py $*"
mkdir -p "$OUT"

tap()   { $G touch "$1" "$2" >/dev/null; sleep "${3:-0.6}"; }
swipe() { $G swipe "$1" "$2" "$3" "$4" 300 >/dev/null; sleep 0.8; }
shot()  { local name=$1; shift; $G screenshot -o "$OUT/$name.png" "$@"; }

page1() { swipe 60 120 260 120; }    # home: swipe right -> page 1
page2() { swipe 260 120 60 120; }    # home: swipe left  -> page 2
back()  { swipe 40 200 260 200; }    # full-screen page: swipe right -> home

# Full-screen pages: tab bar at the top, option buttons at the bottom.
TAB_LEFT="80 17"
TAB_RIGHT="240 17"
BTN_LEFT="84 217"

# Start from home: a back swipe on any page returns home (on home it just
# turns the launcher page, which page1/page2 below take care of).
back

# Home, both launcher pages
page1
shot home
page2
shot home_page2

# Settings (page 2, first icon) -> Display tab
tap 60 98
tap $TAB_LEFT
shot settings_display

# Settings -> Network tab (rescan, wait), private text blurred:
#   status line (SSID + IP), timezone in the dropdown, SSIDs in the list.
tap $TAB_RIGHT 2.0
tap 252 59 6.0          # refresh: the automatic scan can miss right after boot
shot settings_network \
    --blur 6,42,220,35 \
    --blur 36,88,246,28 \
    --blur 40,124,186,116
tap $TAB_LEFT
back

# Diagnostics (page 2, second icon) -> I2C and Power tabs
tap 160 98 1.0
tap $TAB_LEFT 1.0
shot diag_i2c
tap $TAB_RIGHT 1.5
shot diag_power
back

# Clock (page 1, first icon): opens in the saved style; capture both, restore.
page1
tap 60 98 1.5
shot clock_a
tap $BTN_LEFT 1.5
shot clock_b
tap $BTN_LEFT 0.6
back

# Hopper (page 1, third icon): start screen.
tap 260 98 1.0
shot game
back

# Spectrum (page 1, second icon): capture the current view, toggle, capture.
tap 160 98 2.5
shot spectrum_a
tap 160 120 4.0
shot spectrum_b
back

# Pages reopen in their last-used view, so name pairs by content:
# the larger (busier) PNG is the analog dial / the waterfall.
name_pair() {   # name_pair a b big_name small_name
    local a b
    a=$(stat -c %s "$OUT/$1.png"); b=$(stat -c %s "$OUT/$2.png")
    if [ "$a" -gt "$b" ]; then
        mv "$OUT/$1.png" "$OUT/$3.png"; mv "$OUT/$2.png" "$OUT/$4.png"
    else
        mv "$OUT/$1.png" "$OUT/$4.png"; mv "$OUT/$2.png" "$OUT/$3.png"
    fi
}
name_pair clock_a clock_b clock_analog clock_digital
name_pair spectrum_a spectrum_b spectrum_waterfall spectrum_bars
echo "Screenshots in $OUT"
