#!/usr/bin/env bash
# Regenerate every render golden from the current build. Use this
# when you intentionally changed something that shifts the render
# (new feature, updated font, renderer refactor) and want to lock
# in the new look.
#
# WARNING: this OVERWRITES the existing goldens. If you're not sure
# whether the change was intentional, leave the goldens alone, run
# `ctest`, inspect the diffs, and only call this script after a
# human has confirmed the new output is correct.
#
# Usage: scripts/capture-render-goldens.sh [build-dir]
#   build-dir defaults to ./build.

set -euo pipefail

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CORPUS="${REPO_ROOT}/fixtures/bbm-corpus"
GOLDENS="${REPO_ROOT}/fixtures/render-goldens"
PARTS="${REPO_ROOT}/parts/BlueBrickParts/parts"

RENDER_BIN="${BUILD_DIR}/src/app/bld_render"
if [[ ! -x "${RENDER_BIN}" ]]; then
    echo "bld_render not built at ${RENDER_BIN}. Run: cmake --build ${BUILD_DIR}" >&2
    exit 1
fi

mkdir -p "${GOLDENS}"
export QT_QPA_PLATFORM=offscreen

count=0
for bbm in "${CORPUS}"/*.bbm; do
    [[ -f "${bbm}" ]] || continue
    stem="$(basename "${bbm}" .bbm)"
    out="${GOLDENS}/${stem}.png"
    echo "=> ${stem}"
    # The test harness renders at 1600x1200 with the parts library
    # and the map's own background color. bld_render mirrors that
    # exact pipeline (width argument matches; parts dir is searched
    # at the submodule path by default).
    "${RENDER_BIN}" "${bbm}" "${out}" 1600 "${PARTS}"
    count=$((count + 1))
done

echo
echo "Captured ${count} goldens under ${GOLDENS}."
echo "Commit the PNGs to lock them in as the new reference."
