#!/usr/bin/env bash
# Capture documentation screenshots from a running device over the serial
# console (tools/gizmo.py). Drives the UI with simulated taps, so the device
# must be on the home screen and idle.
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

tap()  { $G touch "$1" "$2" >/dev/null; sleep "${3:-0.6}"; }
shot() { local name=$1; shift; $G screenshot -o "$OUT/$name.png" "$@"; }

BACK="33 20"
TAB_LEFT="80 57"
TAB_RIGHT="240 57"

# Home
tap $BACK 0.8
shot home

# Settings -> Display tab
tap 60 98
tap $TAB_LEFT
shot settings_display

# Settings -> Network tab (wait for the scan), private text blurred:
#   status line (SSID + IP), timezone in the dropdown, SSIDs in the list.
tap $TAB_RIGHT 6.0
shot settings_network \
    --blur 6,82,220,35 \
    --blur 36,128,246,28 \
    --blur 40,164,186,76
tap $TAB_LEFT
tap $BACK 0.8

# Diagnostics -> I2C and Power tabs
tap 260 98 1.0
tap $TAB_LEFT 1.0
shot diag_i2c
tap $TAB_RIGHT 1.5
shot diag_power
tap $BACK 0.8

# Spectrum: let it run, then capture the current view, toggle, capture again.
tap 160 98 2.5
shot spectrum_a
tap 160 140 4.0
shot spectrum_b
tap $BACK 0.8

# The page reopens in whichever view was used last, so name the pair by
# content: the waterfall compresses much worse than the flat bars view.
a=$(stat -c %s "$OUT/spectrum_a.png"); b=$(stat -c %s "$OUT/spectrum_b.png")
if [ "$a" -gt "$b" ]; then
    mv "$OUT/spectrum_a.png" "$OUT/spectrum_waterfall.png"; mv "$OUT/spectrum_b.png" "$OUT/spectrum_bars.png"
else
    mv "$OUT/spectrum_a.png" "$OUT/spectrum_bars.png"; mv "$OUT/spectrum_b.png" "$OUT/spectrum_waterfall.png"
fi
echo "Screenshots in $OUT"
