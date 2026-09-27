#!/usr/bin/env bash
# Compatibility check against vanilla BlueBrick 1.9.2 (see README.md here).
#
#   compat.sh <BlueBrick dir with bbconv.exe> <bld-convert> <parts dir> [maps...]
#
# For each map (default: the .bbm files in fixtures/):
#   1. we open and save it; vanilla BlueBrick must read our .bbm and see the
#      same bricks, links included (vanilla re-saves it and we compare);
#   2. for each other format (.ldr .mpd .tdl .ncp) we and vanilla both
#      export it, vanilla reads both exports back, and the results must
#      match.
# Parts the library lacks are skipped in comparisons (BlueBrick redraws
# their placeholders at a different size).
#
# BB_RUNNER picks how BlueBrick runs: "mono" (default, native Mono) or
# "wine" (paths are converted with winepath). Both run under xvfb-run.
set -uo pipefail

bb=$(cd "$1" && pwd)
convert=$2
parts=$3
shift 3
src=$(cd "$(dirname "$0")/../.." && pwd)
maps=("$@")
[ ${#maps[@]} -eq 0 ] && maps=("$src"/fixtures/bbm-corpus/*.bbm "$src"/fixtures/bluebrick-oracle/fourdbrix.bbm "$src"/fixtures/bluebrick-oracle/flex-in.bbm)
runner=${BB_RUNNER:-mono}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

winpath() { if [ "$runner" = wine ]; then winepath -w "$1" 2>/dev/null; else echo "$1"; fi; }
# vanilla <in> <out> [<in> <out> ...]: convert with BlueBrick's own SaveLoadManager.
vanilla() {
    local args=()
    for f in "$@"; do args+=("$(winpath "$f")"); done
    (cd "$bb" && timeout 600 xvfb-run -a "$runner" ./bbconv.exe "${args[@]}") > "$work/vanilla.log" 2>&1
    local status=$?
    [ $status -eq 0 ] || { echo "   vanilla BlueBrick failed ($status):"; grep -v "fixme\|err:" "$work/vanilla.log" | tail -15; }
    return $status
}
same() {  # same <what> <a> <b> [options]
    local what=$1 a=$2 b=$3; shift 3
    if out=$("$convert" -p "$parts" --compare "$a" "$b" --known-only "$@"); then
        echo "   ok    $what ($out)"
    else
        echo "   FAIL  $what"; echo "$out" | sed 's/^/         /'; failed=1
    fi
}

failed=0
for map in "${maps[@]}"; do
    name=$(basename "$map" .bbm)
    d="$work/$name"; mkdir -p "$d"
    echo "== $name"
    # 1. Vanilla reads what we save.
    "$convert" -p "$parts" "$map" "$d/ours.bbm" || { echo "   FAIL  we could not open/save it"; failed=1; continue; }
    vanilla "$d/ours.bbm" "$d/ours.vanilla.bbm" || { failed=1; continue; }
    same ".bbm we save, as vanilla reads it" "$d/ours.bbm" "$d/ours.vanilla.bbm"

    # 2. Exports: ours and vanilla's, both read back by vanilla.
    pairs=()
    for fmt in ldr mpd tdl ncp; do
        "$convert" -p "$parts" "$map" "$d/ours.$fmt" || { echo "   FAIL  we could not export .$fmt"; failed=1; }
        pairs+=("$map" "$d/vanilla.$fmt")
    done
    vanilla "${pairs[@]}" || { failed=1; continue; }
    back=()
    for fmt in ldr mpd tdl ncp; do back+=("$d/ours.$fmt" "$d/ours.$fmt.bbm" "$d/vanilla.$fmt" "$d/vanilla.$fmt.bbm"); done
    vanilla "${back[@]}" || { failed=1; continue; }
    for fmt in ldr mpd tdl ncp; do
        same ".$fmt export, as vanilla reads it" "$d/vanilla.$fmt.bbm" "$d/ours.$fmt.bbm"
    done
done
[ $failed -eq 0 ] && echo "Compatible with vanilla BlueBrick." || echo "Differences from vanilla BlueBrick found."
exit $failed
