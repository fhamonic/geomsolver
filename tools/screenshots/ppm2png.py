#!/usr/bin/env python3
"""usage: tools/screenshots/ppm2png.py IN.ppm OUT.png [X Y W H]

Converts a binary PPM (P6, maxval 255), as the GUI's `shot` writes it, into
an RGB PNG, cropped to the W x H pixels whose top-left corner is (X, Y) when
the crop is given. Only the standard library: no image package is needed.

The PNG holds no time stamp and is compressed the same way on every run, so
the same PPM always gives the same bytes: a regenerated screenshot that did
not change shows no difference in git.
"""
import struct
import sys
import zlib


def read_ppm(path):
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        sys.exit(f"{path}: {e.strerror}")
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos)
            continue
        end = pos
        while end < len(data) and not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    if fields[0] != b"P6" or fields[3] != b"255":
        sys.exit(f"{path}: not a binary PPM with maxval 255")
    width, height = int(fields[1]), int(fields[2])
    pixels = data[pos + 1:pos + 1 + width * height * 3]
    if len(pixels) != width * height * 3:
        sys.exit(f"{path}: {width}x{height} pixels announced, fewer stored")
    return width, height, pixels


def chunk(kind, body):
    return (struct.pack(">I", len(body)) + kind + body
            + struct.pack(">I", zlib.crc32(kind + body)))


def write_png(path, width, height, rows):
    # Filter type 0 on every row: on these flat images it compresses better
    # than the row filters chosen by a cheap heuristic, and costs nothing.
    raw = b"".join(b"\0" + row for row in rows)
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", header))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def main(argv):
    if len(argv) not in (3, 7):
        sys.exit(__doc__.splitlines()[0])
    width, height, pixels = read_ppm(argv[1])
    x, y, w, h = map(int, argv[3:]) if len(argv) == 7 else (0, 0, width,
                                                             height)
    if x < 0 or y < 0 or w <= 0 or h <= 0 or x + w > width or y + h > height:
        sys.exit(f"{argv[1]}: crop {x} {y} {w} {h} does not fit in "
                 f"{width}x{height}")
    stride = width * 3
    rows = [pixels[r * stride + x * 3:r * stride + (x + w) * 3]
            for r in range(y, y + h)]
    write_png(argv[2], w, h, rows)


if __name__ == "__main__":
    main(sys.argv)
