#!/usr/bin/env bash
# clang-format and clang-tidy on the lines a change touches (the code base
# predates .clang-format, so whole files would drown the change in noise).
#
#   scripts/ci/lint-changes.sh [base ref, default origin/main] [build dir with compile_commands.json]
#
# Configure the build dir with clang (Qt adds GCC-only flags clang-tidy
# rejects otherwise).
#
# Fails when clang-tidy's bug-finding checks flag a changed line. Formatting
# is advisory: much of the code is hand-aligned in ways clang-format can't
# keep, so differences are reported (as a GitHub warning in CI) but don't
# fail. The wider style checks in .clang-tidy stay advisory too: run
# clang-tidy yourself to see them.
set -uo pipefail
base=${1:-origin/main}
build=${2:-build}
cd "$(git rev-parse --show-toplevel)"
paths=(src tests fuzz)
status=0

echo "== clang-format (changed lines since $base)"
fmt=$(git clang-format --diff --extensions cpp,h "$base" -- "${paths[@]}" 2>&1)
if [ -z "$fmt" ] || echo "$fmt" | grep -qE "^(no modified files to format|clang-format did not modify any files)"; then
    echo "   ok"
else
    echo "$fmt"
    echo "   Suggested formatting for changed lines (advisory): git clang-format $base"
    [ -n "${GITHUB_ACTIONS:-}" ] && echo "::warning title=clang-format::Changed lines differ from .clang-format (advisory); see the lint log or run git clang-format $base"
fi

echo "== clang-tidy (changed lines since $base)"
tidy_diff=${CLANG_TIDY_DIFF:-$(command -v clang-tidy-diff.py || command -v clang-tidy-diff-18.py || ls /usr/share/clang/clang-tidy-diff.py 2>/dev/null)}
if [ ! -f "$build/compile_commands.json" ]; then
    echo "   no $build/compile_commands.json (configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)"; exit 1
fi
checks='-*,bugprone-*,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions,-bugprone-throwing-static-initialization,clang-analyzer-*,-clang-analyzer-optin.*,performance-*,-performance-enum-size'
# src only: gtest's ASSERT macros read as unchecked optional access.
out=$(git diff -U0 "$base" -- src | python3 "$tidy_diff" -p1 -path "$build" -quiet \
        -iregex '.*\.(cpp|h)' -checks="$checks" -j "$(nproc)" 2>&1)
if echo "$out" | grep -qE "(warning|error): "; then
    echo "$out" | grep -E -A3 "(warning|error): " | head -200
    echo "   clang-tidy found problems on changed lines"
    status=1
else
    echo "   ok"
fi
exit $status
