#!/usr/bin/env bash
# Build the parser fuzzers with clang and run each for a while.
#   fuzz/run.sh [seconds per target, default 60] [build dir, default build-fuzz]
# Crashes land in <build dir>/fuzz-artifacts/<target>/ and fail the run.
set -euo pipefail
secs=${1:-60}
build=${2:-build-fuzz}
src=$(cd "$(dirname "$0")/.." && pwd)

cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DBLD_FUZZ=ON -DBUILD_TESTING=OFF ${BLD_FUZZ_CMAKE_ARGS:-} > /dev/null
targets=(bbm sidecar venue part_xml budget ldraw_map tdl ncp ldraw_import lxfml studio)
cmake --build "$build" --target bld_fuzz_seeds "${targets[@]/#/fuzz_}"

seeds="$build/fuzz-seeds"
rm -rf "$seeds" && "$build/fuzz/bld_fuzz_seeds" "$seeds"
oracle="$src/fixtures/bluebrick-oracle"
corpus() {  # corpus <target> <files...>
    mkdir -p "$build/fuzz-corpus/$1"
    shift_target=$1; shift
    for f in "$@"; do [ -f "$f" ] && cp "$f" "$build/fuzz-corpus/$shift_target/"; done
    return 0
}
corpus bbm "$src"/fixtures/bbm-corpus/*.bbm "$oracle"/*.bbm
corpus sidecar "$seeds"/sidecar/*
corpus venue "$seeds"/venue/*
corpus part_xml $(find "$src/parts/BlueBrickParts/parts" -name '*.xml' | sort | awk 'NR % 40 == 1')
corpus budget "$oracle"/budget*.bbb "$seeds"/budget/*
corpus ldraw_map "$oracle"/*.ldr "$oracle"/*.mpd
corpus tdl "$oracle"/*.tdl
corpus ncp "$oracle"/*.ncp
corpus ldraw_import "$oracle"/*.ldr "$oracle"/*.mpd
corpus lxfml "$seeds"/lxfml/*
corpus studio "$seeds"/studio/*

export QT_QPA_PLATFORM=offscreen
# alloc_dealloc_mismatch: std::stable_sort's temporary buffer (libstdc++ 16)
# trips it in Qt and in our code alike; not a real mismatch.
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=1:alloc_dealloc_mismatch=0
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
status=0
for t in "${targets[@]}"; do
    mkdir -p "$build/fuzz-artifacts/$t"
    echo "== $t (${secs}s)"
    if ! "$build/fuzz/fuzz_$t" -max_total_time="$secs" -timeout=20 -rss_limit_mb=4096 \
            -artifact_prefix="$build/fuzz-artifacts/$t/" "$build/fuzz-corpus/$t" 2> "$build/fuzz-artifacts/$t/log.txt"; then
        echo "   FAILED, see $build/fuzz-artifacts/$t/"
        tail -40 "$build/fuzz-artifacts/$t/log.txt"
        status=1
    else
        grep -E "^#[0-9]+.*DONE" "$build/fuzz-artifacts/$t/log.txt" | tail -1 || true
    fi
done
exit $status
