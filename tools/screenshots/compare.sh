#!/usr/bin/env bash
# usage: tools/screenshots/compare.sh REF_DIR NEW_DIR
#
# Compares every PPM of REF_DIR with NEW_DIR's file of the same name, byte
# for byte. A scene that differs gets the bounding box and the pixel count of
# its difference; a scene of NEW_DIR that REF_DIR lacks is listed as new.
# Exit status: the number of scenes that differ or are missing.
set -u
if [ $# -ne 2 ] || [ ! -d "$1" ] || [ ! -d "$2" ]; then
    echo "usage: $0 REF_DIR NEW_DIR" >&2
    exit 255
fi
REF=$(realpath "$1")
NEW=$(realpath "$2")
bad=0
for ref in "$REF"/*.ppm; do
    [ -e "$ref" ] || continue
    name=$(basename "$ref")
    if [ ! -f "$NEW/$name" ]; then
        echo "MISSING  $name"
        bad=$((bad + 1))
    elif cmp -s "$ref" "$NEW/$name"; then
        echo "same     $name"
    else
        bad=$((bad + 1))
        python3 -I - "$ref" "$NEW/$name" <<'PY'
import sys

def read(path):
    with open(path, "rb") as f:
        data = f.read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    return int(fields[1]), int(fields[2]), data[pos + 1:]

wa, ha, a = read(sys.argv[1])
wb, hb, b = read(sys.argv[2])
name = sys.argv[1].rsplit("/", 1)[-1]
if (wa, ha) != (wb, hb):
    print(f"DIFFERS  {name}: size {wa}x{ha} vs {wb}x{hb}")
    sys.exit()
x0, y0, x1, y1, n = wa, ha, -1, -1, 0
for i in range(0, len(a), 3):
    if a[i:i + 3] != b[i:i + 3]:
        p = i // 3
        x, y = p % wa, p // wa
        x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
        n += 1
print(f"DIFFERS  {name}: {n} pixel(s) in x {x0}..{x1}, y {y0}..{y1}")
PY
    fi
done
for new in "$NEW"/*.ppm; do
    [ -e "$new" ] || continue
    [ -f "$REF/$(basename "$new")" ] || echo "NEW      $(basename "$new")"
done
exit $bad
