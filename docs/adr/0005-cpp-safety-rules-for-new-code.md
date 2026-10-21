# 5. C++ safety rules for new code, enforced by tiered gates

Date: 2026-10-21. Status: accepted.

## Context

The fork adds C++ (render device, adapters, platform seam) beside upstream's, and most of it is
written by agents. Memory errors, leaks, races, undefined behaviour, unsafe casts, narrowing and
ownership bugs must fail a machine check before review, not reach it. Upstream code is never
reformatted or newly warned on (ADR 1), so every gate is scoped to the directories in
`tools/dev/fork-dirs`. Ticket #56 builds the rules, the editor and pre-commit tiers and the gate
self-test; #58 adds the check tier (sanitizers, `leaks`, hardening, GPU validation), #59 the
nightly tier (TSan, GCC `-fanalyzer`, cppcheck, CodeQL).

## Decision

**Rules for fork code** (`docs/agents/conventions.md` holds the idioms):

- RAII for every resource. SDL handles live in `std::unique_ptr` with the SDL release function as
  the deleter (`std::unique_ptr<SDL_Window, sdl_deleter<&SDL_DestroyWindow>>`); no explicit
  `*_close` / `free_*` teardown.
- No `new` or `delete`: `std::make_unique`, containers, `RAIIdmem`.
- No unchecked casts: no C-style casts, `reinterpret_cast`, `const_cast` or `static_cast`
  downcasts. A `static_cast` from SDL's `void *` happens only in the one wrapper layer that turns
  SDL userdata into a type; SDL's `void *` goes no further.
- Domain values are `enum class` with an explicit underlying type or strong types
  (`valptridx`), never bare integers.
- Fixed collections are `std::array` or `enumerated_array`, views `std::span`; no raw arrays and
  no pointer-plus-length pairs.
- Logging stays `con_printf`, which is format-checked: the varargs rule does not apply to it.

**Gates, tiered by feedback time.** Each tier's budget is checked by a test, not only stated.

| Tier | Budget | Runs | Checked by |
|---|---|---|---|
| editor | < 1 s | clangd with the fork directory's `.clang-tidy` (a copy of `tools/dev/style/clang-tidy`) | `gates_test.sh` |
| pre-commit | < 10 s | the hook: commit size, then `check.sh --pre-commit`: repo config, licensed-data guard, `format.sh --staged` (clang-format, NOLINT reasons and clang-tidy on the staged fork files) | `gates_test.sh`, timing the real hook |
| check | minutes | `check.sh --fast` (shell tests, shellcheck, format of every fork file) and `check.sh`: unit tests, `format.sh --tidy` on every fork file, the real-build strict-warning test; #58 adds the rest | `check.sh` |
| nightly | none | #59 | #59 |

clangd skips `clang-analyzer-*`, `bugprone-use-after-move` and slow checks by design, so the
editor tier shows the fast checks and the pre-commit tier is the first to run them all.

**Strict warnings for fork files**: `-Wconversion -Wsign-conversion -Wshadow -Werror`, added by
`tools/dev/fork_warnings.py` from the site file `build.sh` reads, to every compile whose source
lies in a fork directory. `SConstruct` is unchanged; upstream files compile as before. The flags
reach the compilation database, and clang-tidy's `clang-diagnostic-*` reports them before a build.

**Hardened standard library** in every `build.sh` profile (dev and asan), for upstream and fork
code alike: `_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG` (libc++, the Mac) and
`_GLIBCXX_ASSERTIONS` (libstdc++, the container), from `tools/dev/hardening.py` through
`site-dev.py`, so container bounds, iterator and optional misuse trap at run time.

**GPU validation is a gate**: every SDL_GPU run in a check runs under `tools/dev/gpu-validate.sh`,
with SDL's GPU debug mode (the program creates its device with `debug_mode` true), Metal API
validation (`MTL_DEBUG_LAYER=1`) and the Vulkan validation layer
(`VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn,perf`). Neither SDL nor the layer changes the exit
status (spike #55, K4), so the wrapper scans the output and fails on any validation message, and
on a run whose validation was not active. Until the fork has GPU code (#24), headless probes in
the gate self-test prove it on Metal natively and on lavapipe in the container.

**Justified exceptions** are inline only, `// NOLINT(check): reason` or
`// NOLINTNEXTLINE(check): reason`; `format.sh` fails on a NOLINT without a check list and a
reason.

**Ratchet.** The enabled set is the one that is clean, with zero false alarms, on the
representative fork file `tools/dev/tests/gates/clean.cpp.in` (the conventions' idioms). Each
tightening is its own small commit that leaves the tree green and updates the level below.

- **Level 1 (active)**: `clang-diagnostic-*`, `clang-analyzer-*`, `bugprone-*` (with
  `bugprone-use-after-move`) except `bugprone-easily-swappable-parameters`,
  `cppcoreguidelines-owning-memory`, `cppcoreguidelines-pro-type-*` except
  `cppcoreguidelines-pro-type-vararg`, `cppcoreguidelines-narrowing-conversions`,
  `modernize-use-using`, `modernize-use-nullptr`,
  `modernize-use-override`, `readability-identifier-naming` (lower snake case),
  `misc-const-correctness`; `WarningsAsErrors: '*'`; strict warnings as above.
- Left out for the whole config, not suppressed inline: the rule that exceptions are inline
  NOLINTs with a reason is for a single justified site, and these two checks fire on idioms fork
  code uses throughout, so every fork file would carry the same NOLINT and a real exception would
  be lost among them. Each is a level-1 exclusion that a later level retires:
  - `cppcoreguidelines-pro-type-vararg` fires on every `con_printf` call, the logging the rules
    prescribe and which the compiler format-checks. It comes back on when fork logging goes
    through a non-variadic typed wrapper, and `con_printf` calls in fork code become the inline
    exceptions.
  - `bugprone-easily-swappable-parameters` (already off before #56) flags every pair of adjacent
    parameters of one type, such as an SDL-shaped `(int w, int h)`. It comes back on when the
    strong-types rule has reached the fork's interfaces, so that adjacent same-typed parameters
    are rare enough to justify one by one.

**Gate self-test.** `tools/dev/tests/gates_test.sh` copies each planted file in
`tools/dev/tests/gates/` into a throwaway repository and asserts that every gate the file names
goes red with the expected diagnostic, that `clean.cpp.in` passes them all, and that every
check the clang-tidy config enables is named by some planted file's expected diagnostic. A gate
a later ticket owns shows as a SKIP naming that ticket until it defines the gate's runner.

## Consequences

- A new fork directory needs a line in `fork-dirs` and a copy of the clang-tidy config; the
  pre-commit gate says so when one is missing or stale.
- clang-tidy needs `build/dev/compile_commands.json`; a fork file not yet in it gets its
  neighbour's flags, and the pre-commit gate fails with a hint when there is no database.
- The pre-commit gate tidies the working-tree file, so a partly staged file is checked as it
  stands on disk. C files get clang-format only: the clang-tidy rules are C++ rules.
- The hook runs only the pre-commit tier, so a commit takes seconds. The rest of
  `check.sh --fast`, the shell tests among them, runs in `check.sh` and `ci.sh`, which a
  ticket's report requires; a commit can pass the hook and still fail them.
- Run-time leaks are found by LeakSanitizer in the container and by `leaks --atExit`
  (`tools/dev/leaks.sh`) on the Mac, where LeakSanitizer is unsupported on Apple Silicon. `leaks`
  drops the program's exit status, so the leaks step relies on the unit_tests step for it.
- Data races are caught only once #59 lands; the self-test lists the gate as a SKIP until then.
