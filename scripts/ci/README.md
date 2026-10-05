# CI helper scripts

- `lint-changes.sh [base] [build dir]` — clang-tidy's bug-finding checks
  (blocking) and clang-format (advisory) on the lines changed since `base`.
  The code base predates `.clang-format` and is partly hand-aligned, so only
  changed lines are checked. Needs a clang-configured build directory with
  `compile_commands.json` (exported by default). The *Lint changed lines*
  CI job runs it on every pull request.

- `check-warnings.sh <log>...` — fails when a configure or build log has a
  compiler, linker or CMake warning in it. The *Build* jobs run it after
  building with `-DBLD_WARNINGS_AS_ERRORS=ON`, so the build stays
  warning-free on all three platforms.

The *Coverage* CI job reports line, function and branch coverage of `src/`
(job summary plus an HTML artifact); to run it locally, configure with
`--coverage` in the C/C++ and linker flags, run the tests, then
`gcovr -r . --object-directory <build> --filter src/ --exclude src/app/ --print-summary`.
