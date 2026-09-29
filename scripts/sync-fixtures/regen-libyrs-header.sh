#!/bin/sh
# Regenerates third_party/yrs/libyrs.h, yffi's C header, for the yffi version
# src/sync/CMakeLists.txt pins. Run after bumping that version (and its
# URL_HASH). Needs cargo and cbindgen (cargo install cbindgen).
#
#   scripts/sync-fixtures/regen-libyrs-header.sh 0.28.0
set -eu
VERSION=${1:?usage: regen-libyrs-header.sh <yffi version>}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
curl -sSfL "https://static.crates.io/crates/yffi/yffi-$VERSION.crate" | tar -xz -C "$TMP"
cd "$TMP/yffi-$VERSION"
cbindgen --config cbindgen.toml --crate yffi --output "$ROOT/third_party/yrs/libyrs.h"
echo "wrote third_party/yrs/libyrs.h for yffi $VERSION"
