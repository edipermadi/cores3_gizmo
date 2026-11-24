#!/usr/bin/env python3
"""Host side of the gizmo serial console.

    gizmo.py screenshot [-o out.png] [--blur X,Y,W,H ...]
                                         grab the screen as PNG, optionally
                                         blurring regions (e.g. SSIDs) first
    gizmo.py touch X Y [MS]              simulate a tap (MS = hold time)
    gizmo.py swipe X1 Y1 X2 Y2 [MS]      simulate a drag (MS = duration)
    gizmo.py cmd "any console command"   send a raw command, print the reply

The port is auto-detected (Espressif USB-Serial-JTAG) or given with --port.
Needs pyserial (present in the ESP-IDF Python environment).
"""
import argparse
import base64
import glob
import re
import struct
import sys
import time
import zlib

import serial

B64_LINE = re.compile(r"^[A-Za-z0-9+/=]+$")
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def find_port():
    ports = sorted(glob.glob("/dev/serial/by-id/*Espressif*"))
    if not ports:
        sys.exit("No Espressif USB serial device found; pass --port")
    return ports[0]


def open_port(path):
    # USB-Serial-JTAG resets the chip when RTS is asserted while DTR is not.
    # Open with pyserial's defaults (both asserted: no reset), then release
    # RTS before DTR so that state never occurs. Releasing DTR first (as
    # pyserial does when both are set False before open) resets the board.
    s = serial.Serial(path, 115200, timeout=0.2)
    s.rts = False
    s.dtr = False
    return s


def send(s, line):
    s.reset_input_buffer()
    s.write(b"\r\n")           # finish any partial line at the prompt
    time.sleep(0.1)
    s.reset_input_buffer()
    s.write(line.encode() + b"\r\n")


def read_lines(s, until, timeout):
    """Yield decoded lines until one matches `until` (regex) or timeout."""
    end = time.time() + timeout
    buf = b""
    while time.time() < end:
        buf += s.read(4096)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = ANSI.sub("", raw.decode(errors="replace")).strip("\r ")
            yield line
            if re.search(until, line):
                return
    raise TimeoutError(f"no '{until}' within {timeout}s")


def rgb565le_to_rgb(raw, w, h):
    """RGB565 little-endian -> flat RGB888 bytearray."""
    rgb = bytearray()
    for (px,) in struct.iter_unpack("<H", raw[:w * h * 2]):
        r, g, b = (px >> 11) & 0x1F, (px >> 5) & 0x3F, px & 0x1F
        rgb += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return rgb


def blur_region(rgb, w, h, x0, y0, rw, rh, radius=6, passes=3):
    """Strong blur of one rectangle in place (repeated box blur ~ Gaussian).

    Only pixels inside the rectangle are read, so nothing outside leaks in and
    the private content is not recoverable from the blurred result.
    """
    x0, y0 = max(0, x0), max(0, y0)
    x1, y1 = min(w, x0 + rw), min(h, y0 + rh)
    if x1 <= x0 or y1 <= y0:
        return
    for _ in range(passes):
        for axis in (0, 1):
            outer = range(y0, y1) if axis == 0 else range(x0, x1)
            span = (x0, x1) if axis == 0 else (y0, y1)
            for o in outer:
                idx = [(o * w + i) * 3 for i in range(*span)] if axis == 0 else \
                      [(i * w + o) * 3 for i in range(*span)]
                n = len(idx)
                src = [rgb[i:i + 3] for i in idx]
                for k in range(n):
                    lo, hi = max(0, k - radius), min(n, k + radius + 1)
                    cnt = hi - lo
                    for c in range(3):
                        rgb[idx[k] + c] = sum(src[j][c] for j in range(lo, hi)) // cnt


def rgb_to_png(rgb, w, h):
    rows = bytearray()
    for y in range(h):
        rows.append(0)  # filter: none
        rows += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
            + chunk(b"IEND", b""))


def cmd_screenshot(s, out, blur=()):
    send(s, "screenshot")
    header, b64, stats = None, [], ""
    for line in read_lines(s, r"^SCREENSHOT END|screenshot: ", timeout=20):
        if line.startswith("screenshot: "):
            sys.exit(f"device: {line}")
        m = re.search(r"SCREENSHOT BEGIN w=(\d+) h=(\d+) fmt=(\S+) raw=(\d+) zlen=(\d+)", line)
        if m:
            header = m
            continue
        if header and B64_LINE.match(line):
            b64.append(line)
    # Timing/ratio line follows END.
    try:
        stats = next(read_lines(s, r".", timeout=2))
    except (TimeoutError, StopIteration):
        pass

    if not header:
        sys.exit("no SCREENSHOT BEGIN seen")
    w, h, fmt, raw_len, zlen = int(header[1]), int(header[2]), header[3], int(header[4]), int(header[5])
    if fmt != "rgb565le":
        sys.exit(f"unsupported format {fmt}")
    data = base64.b64decode("".join(b64))
    if len(data) != zlen:
        sys.exit(f"got {len(data)} compressed bytes, expected {zlen} (log lines interleaved?)")
    raw = zlib.decompress(data)
    if len(raw) != raw_len:
        sys.exit(f"decompressed {len(raw)} bytes, expected {raw_len}")

    rgb = rgb565le_to_rgb(raw, w, h)
    for region in blur:
        blur_region(rgb, w, h, *region)
    with open(out, "wb") as f:
        f.write(rgb_to_png(rgb, w, h))
    if blur:
        print(f"blurred {len(blur)} region(s)")
    print(f"{out}: {w}x{h}, {zlen} bytes over serial ({100 * zlen / raw_len:.1f}% of raw)")
    if stats:
        print(f"device: {stats}")


def cmd_touch(s, x, y, ms):
    line = f"touch {x} {y}" + (f" {ms}" if ms else "")
    send(s, line)
    for reply in read_lines(s, r"^touched |out of range|Command returned", timeout=15):
        if reply.startswith(("touched ", "out of range")):
            print(reply)
            return


def cmd_swipe(s, x1, y1, x2, y2, ms):
    line = f"swipe {x1} {y1} {x2} {y2}" + (f" {ms}" if ms else "")
    send(s, line)
    for reply in read_lines(s, r"^swiped |out of range|Command returned", timeout=15):
        if reply.startswith(("swiped ", "out of range")):
            print(reply)
            return


def cmd_raw(s, line, wait):
    send(s, line)
    end = time.time() + wait
    while time.time() < end:
        data = s.read(4096)
        if data:
            sys.stdout.write(ANSI.sub("", data.decode(errors="replace")))
            sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: auto-detect)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("screenshot", help="save the screen as PNG")
    p.add_argument("-o", "--out", default="screenshot.png")
    p.add_argument("--blur", action="append", default=[], metavar="X,Y,W,H",
                   type=lambda v: tuple(int(n) for n in v.split(",")),
                   help="blur this rectangle before saving (repeatable)")

    p = sub.add_parser("touch", help="simulate a tap")
    p.add_argument("x", type=int)
    p.add_argument("y", type=int)
    p.add_argument("ms", type=int, nargs="?")

    p = sub.add_parser("swipe", help="simulate a drag")
    for name in ("x1", "y1", "x2", "y2"):
        p.add_argument(name, type=int)
    p.add_argument("ms", type=int, nargs="?")

    p = sub.add_parser("cmd", help="send a raw console command")
    p.add_argument("line")
    p.add_argument("--wait", type=float, default=2.0, help="seconds to print output")

    args = ap.parse_args()
    s = open_port(args.port or find_port())
    try:
        if args.cmd == "screenshot":
            cmd_screenshot(s, args.out, args.blur)
        elif args.cmd == "touch":
            cmd_touch(s, args.x, args.y, args.ms)
        elif args.cmd == "swipe":
            cmd_swipe(s, args.x1, args.y1, args.x2, args.y2, args.ms)
        else:
            cmd_raw(s, args.line, args.wait)
    finally:
        s.close()


if __name__ == "__main__":
    main()
