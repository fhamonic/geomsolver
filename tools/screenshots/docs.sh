#!/usr/bin/env bash
# usage: tools/screenshots/docs.sh GEOMSOLVER [OUT_DIR]
#        tools/screenshots/docs.sh --check
#
# Makes the screenshots of the documentation, docs/assets/screenshots/*.png,
# or writes them into OUT_DIR. The manifest below gives, for each PNG, the
# script that draws it (relative to tools/screenshots, without .script), the
# shot it takes in that script, and the crop: the W x H pixels whose top-left
# corner is (X, Y). Each script runs once, from the repository root, with an
# empty temporary XDG_CONFIG_HOME, as render.sh runs the scenes.
#
# --check needs neither the binary nor a display: every PNG that a page of
# docs/ or README.md shows must be in the manifest, every PNG of the manifest
# must be shown by a page and present in docs/assets/screenshots, which holds
# nothing else, and every script must exist.
#
# Exit status: the number of problems, at most 255.
set -u
HERE=$(dirname "$(realpath "$0")")
ROOT=$(realpath "$HERE/../..")
SHOTS=docs/assets/screenshots

manifest() {
    sed -e 's/#.*//' -e '/^[[:space:]]*$/d' <<'EOF'
# PNG                              SCRIPT                 SHOT            X     Y     W     H
# 1600x1000: the window, a canvas with its sweep bar (0 21 1022 669) or
# without one (0 21 1022 636), or a panel.
start-overview                     docs/tv-solved         overview        0     0     1600  1000
example-tv-solved                  docs/tv-solved         solver          0     0     1600  1000
guide-b-criteria-panel             docs/tv-solved         criteria        701   501   899   365
start-gui-properties-point         docs/tv-solved         point           563   421   459   440
start-gui-properties-criterion     docs/tv-solved         criterion       563   421   459   300
example-tv-hand                    docs/tv-hand           hand            0     21    1022  958
start-gui-violated                 docs/tv-hand           hand            0     21    1022  669
start-gui-margins                  docs/tv-hand           hand            0     692   1022  287
start-gui-error                    docs/tv-error          error           0     0     1600  1000
guide-b-troubleshooting-errors     docs/desk-errors       errors          0     0     1600  1000
start-gui-document                 docs/document          document        0     382   561   385
guide-a-domains                    docs/guides            domains         0     0     1600  1000
guide-a-lamp                       docs/guides            lamp            0     0     1600  1000
guide-a-kitchen                    docs/guides            kitchen         0     21    1022  636
guide-a-chain                      docs/guides            chain           0     21    1022  669
guide-a-flap                       docs/guides            flap            0     21    1022  669
guide-b-recipes-study              docs/guides            study           0     21    1022  636
guide-b-recipes-study-naive        docs/guides            study-naive     0     21    1022  636
start-tutorial-room                docs/tutorial          room            0     21    1022  636
start-tutorial-design              docs/tutorial          design          0     21    1022  636
start-tutorial-motion              docs/tutorial          motion          0     21    1022  669
start-tutorial-solved              docs/tutorial          solved          0     0     1600  1000
start-tutorial-violated            docs/tutorial          violated        0     0     1600  1000
start-gui-view-menu                scenes/view-menu       view-menu       0     0     300   314
start-gui-settings                 scenes/solver-settings solver-settings 1024  501   576   478
start-gui-variants                 scenes/solver-variants solver-variants 800   501   800   300
# 1600x1500: the Solver panel, the desk's canvas, Constraints and Margins.
start-gui-pareto                   docs/pareto            views-4         1024  751   576   728
guide-b-pareto-gui                 docs/pareto            views-7         1024  751   576   728
example-tv-pareto-views            docs/pareto            views-7         1024  751   576   728
start-tutorial-pareto              docs/pareto            tutorial        1024  751   576   728
guide-b-display-desk               docs/desk              desk            130   21    892   1076
guide-b-constraints-panel          docs/desk              constraints     701   751   899   195
guide-b-constraints-margins        docs/desk              desk            0     1099  1022  380
# 3200x2000: close-ups of the corner TV's canvas.
guide-b-display-tv                 docs/tv-corner         ghosts          640   1039  680   520
example-tv-couch                   docs/tv-corner         couch           640   620   800   940
example-tv-centred                 docs/tv-corner         centred         640   620   800   940
example-tv-kitchen                 docs/tv-corner         kitchen         640   620   800   940
example-tv-evolved                 docs/tv-corner         evolved         640   860   800   700
start-gui-context-menu             docs/tv-panels         menu            740   1240  460   420
start-gui-box-select               docs/tv-panels         box             600   1150  700   410
start-gui-formula-popup            docs/tv-panels         popup           1300  870   600   325
# 1400x850: the door of the CLI reference.
ref-gui-solve                      docs/door              door            0     0     1400  850
EOF
}

check() {
    local problems=0 listed referenced present script
    cd "$ROOT" || return 1
    listed=$(manifest | awk '{print $1}' | sort)
    referenced=$(grep -rhoE --include='*.md' \
        'screenshots/[A-Za-z0-9_.-]+\.png' docs README.md |
        sed -e 's|.*/||' -e 's|\.png$||' | sort -u)
    present=$(find "$SHOTS" -maxdepth 1 -name '*.png' -printf '%f\n' |
        sed 's|\.png$||' | sort)
    while read -r png; do
        echo "listed twice in the manifest: $png"
        problems=$((problems + 1))
    done < <(uniq -d <<<"$listed")
    while read -r png; do
        echo "shown in the docs, not in the manifest: $png.png"
        problems=$((problems + 1))
    done < <(comm -13 <(uniq <<<"$listed") <(echo "$referenced") | sed '/^$/d')
    while read -r png; do
        echo "in the manifest, shown by no page: $png.png"
        problems=$((problems + 1))
    done < <(comm -23 <(uniq <<<"$listed") <(echo "$referenced") | sed '/^$/d')
    while read -r png; do
        echo "in $SHOTS, not in the manifest: $png.png"
        problems=$((problems + 1))
    done < <(comm -13 <(uniq <<<"$listed") <(echo "$present") | sed '/^$/d')
    while read -r png; do
        echo "in the manifest, missing from $SHOTS: $png.png"
        problems=$((problems + 1))
    done < <(comm -23 <(uniq <<<"$listed") <(echo "$present") | sed '/^$/d')
    while read -r script; do
        if [ ! -f "$HERE/$script.script" ]; then
            echo "no script tools/screenshots/$script.script"
            problems=$((problems + 1))
        fi
    done < <(manifest | awk '{print $2}' | sort -u)
    echo "checked $(uniq <<<"$listed" | wc -l) screenshot(s), $problems problem(s)"
    # A status is taken modulo 256: 256 problems would read as success.
    return $((problems > 255 ? 255 : problems))
}

if [ $# -eq 1 ] && [ "$1" = --check ]; then
    check
    exit $?
fi
if [ $# -lt 1 ] || [ $# -gt 2 ] || [ "${1#-}" != "$1" ]; then
    echo "usage: $0 GEOMSOLVER [OUT_DIR]" >&2
    echo "       $0 --check" >&2
    exit 255
fi
BIN=$(realpath "$1")
OUT=$(realpath -m "${2:-$ROOT/$SHOTS}")
mkdir -p "$OUT" || exit 255
work=$(mktemp -d) || exit 255
config=$(mktemp -d) || exit 255
trap 'rm -rf "$work" "$config"' EXIT
cd "$ROOT" || exit 255

failed=0
while read -r script; do
    mkdir -p "$work/$script"
    if ! XDG_CONFIG_HOME=$config "$BIN" --script "tools/screenshots/$script.script" \
        --shot-dir "$work/$script" >"$work/$script/log.txt" 2>&1; then
        echo "FAILED   $script:"
        cat "$work/$script/log.txt"
        failed=$((failed + 1))
    fi
done < <(manifest | awk '!seen[$2]++ {print $2}')
count=0
while read -r png script shot x y w h; do
    if python3 -I "$HERE/ppm2png.py" "$work/$script/$shot.ppm" "$OUT/$png.png" \
        "$x" "$y" "$w" "$h"; then
        count=$((count + 1))
    else
        echo "FAILED   $png.png, from $script: $shot"
        failed=$((failed + 1))
    fi
done < <(manifest)
if [ -n "$(find "$config" -mindepth 1 -print -quit)" ]; then
    echo "a scripted run wrote settings:"
    find "$config" -mindepth 1
    failed=$((failed + 1))
fi
echo "wrote $count screenshot(s) into $OUT, $failed failure(s)"
exit $failed
