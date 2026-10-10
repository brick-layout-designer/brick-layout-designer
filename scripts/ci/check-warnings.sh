#!/usr/bin/env bash
# Fails when a configure or build log has a warning in it: compiler (GCC,
# clang, MSVC), linker (GNU ld, Apple ld, MSVC link) or CMake. The build is
# warning-free and CI keeps it that way; BLD_WARNINGS_AS_ERRORS already stops
# compiler warnings in our own code, this also catches the linker's, CMake's
# and the third-party code we build.
#
#   scripts/ci/check-warnings.sh <log>...
set -uo pipefail
[ $# -gt 0 ] || { echo "usage: $0 <log>..." >&2; exit 2; }
# Color codes off; "warning:" / "warning C4244:" / "warning LNK4098:",
# "ld: warning:", and "CMake Warning". GitHub's own "##[warning]" notes and
# "N warnings generated." summaries don't match.
found=$(sed -E 's/\x1b\[[0-9;]*m//g' "$@" | grep -E '(^|[^[:alnum:]_\[])warning( [A-Z]+[0-9]+)?:|CMake Warning' | sort -u)
if [ -z "$found" ]; then
    echo "No warnings."
    exit 0
fi
count=$(printf '%s\n' "$found" | wc -l | tr -d ' ')
printf '%s\n' "$found"
echo "::error::$count warning(s) in the build. The build is kept warning-free: fix them (see the lines above)."
exit 1
