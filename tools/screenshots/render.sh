#!/usr/bin/env bash
# usage: tools/screenshots/render.sh GEOMSOLVER OUT_DIR [SCENE...]
#
# Plays scenes/SCENE.script, or every scene, with the GUI binary GEOMSOLVER
# and writes its shots into OUT_DIR, its output into OUT_DIR/logs/SCENE.txt.
# The scenes run from the repository root: their instance paths are relative
# to it, and the images show them as written.
#
# Every run gets the same empty temporary XDG_CONFIG_HOME: a scripted run
# must neither read nor write the user's settings, and a file left there
# fails the render. Exit status: the number of scenes that failed, plus one
# when the settings directory is not empty.
set -u
if [ $# -lt 2 ]; then
    echo "usage: $0 GEOMSOLVER OUT_DIR [SCENE...]" >&2
    exit 255
fi
BIN=$(realpath "$1")
OUT=$(realpath -m "$2")
shift 2
HERE=$(dirname "$(realpath "$0")")
ROOT=$(realpath "$HERE/../..")
if [ $# -gt 0 ]; then
    scenes=("$@")
else
    scenes=()
    for f in "$HERE"/scenes/*.script; do
        scenes+=("$(basename "$f" .script)")
    done
fi
mkdir -p "$OUT/logs" || exit 255
config=$(mktemp -d) || exit 255
trap 'rm -rf "$config"' EXIT
cd "$ROOT" || exit 255

failed=0
for scene in "${scenes[@]}"; do
    log=$OUT/logs/$scene.txt
    XDG_CONFIG_HOME=$config "$BIN" \
        --script "tools/screenshots/scenes/$scene.script" \
        --shot-dir "$OUT" >"$log" 2>&1
    status=$?
    echo "exit $status" >>"$log"
    if [ $status -ne 0 ]; then
        echo "FAILED   $scene (exit $status): $log"
        failed=$((failed + 1))
    fi
done
if [ -n "$(find "$config" -mindepth 1 -print -quit)" ]; then
    echo "a scripted run wrote settings:"
    find "$config" -mindepth 1
    failed=$((failed + 1))
fi
echo "rendered ${#scenes[@]} scene(s) into $OUT, $failed failure(s)"
exit $failed
