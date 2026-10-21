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
  SDL userdata into a type; SDL's `void *` goes no further. The wrapper layer is the fork files
  named `sdl_wrap*` (`sdl_wrap.cpp`, `sdl_wrap.h`, `sdl_wrap_audio.cpp`), and a check enforces it.
- Domain values are `enum class` with an explicit underlying type or strong types
  (`valptridx`), never bare integers.
- Fixed collections are `std::array` or `enumerated_array`, views `std::span`; no raw arrays and
  no pointer-plus-length pairs.
- Logging stays `con_printf`, which is format-checked: the varargs rule does not apply to it.

**Gates, tiered by feedback time.** Each tier's budget is checked by a test, not only stated.

| Tier | Budget | Runs | Checked by |
|---|---|---|---|
| editor | < 1 s | clangd with the fork directory's `.clang-tidy` (a copy of `tools/dev/style/clang-tidy`) | `gates_test.sh` |
| pre-commit | < 10 s | the hook: commit size, then `check.sh --pre-commit`: repo config, licensed-data guard, `format.sh --staged` (clang-format, NOLINT reasons and clang-tidy on the staged fork files), gitleaks on the staged changes (the pinned one, else one at the pinned version on PATH; without either the tier fails, never skips), no `.gitleaksignore` or `gitleaks:allow` unless `.gitleaks.toml` lists its file with a reason | `gates_test.sh`, timing the real hook |
| pre-push | seconds | the hook: `check.sh --pre-push`: gitleaks on exactly the commits being pushed (`<remote>..<local>`, or those on no remote-tracking branch for a new ref), the same pinned lookup, failing without it | `check_test.sh` |
| check | 10 min | `check.sh --fast` (shell tests with the gate self-test, shellcheck, format of every fork file) and `check.sh`: gitleaks over the whole history of HEAD, unit tests (hardened), `leaks.sh` on them (Mac), `format.sh --tidy` on every fork file, the real-build strict-warning test, the unit tests under ASan, UBSan and LSan (LSan in the container); the rtrace tests in both once #19 adds `tools/dev/rtrace-tests.sh` | `check.sh`, timing its own full run |
| nightly | none | #59 | #59 |

The editor and pre-commit budgets are the Mac's, where the owner edits and commits: their tests
pass on the best of three runs on macOS, and on Linux (the container, under amd64 emulation) print
the three times without failing.

clangd skips `clang-analyzer-*`, `bugprone-use-after-move` and slow checks by design, so the
editor tier shows the fast checks and the pre-commit tier is the first to run them all.

**Strict warnings for fork files**: `-Wconversion -Wsign-conversion -Wshadow -Werror`, added by
`tools/dev/fork_warnings.py` from the site file `build.sh` reads, to every compile whose source
lies in a fork directory. `SConstruct` is unchanged; upstream files compile as before. The flags
reach the compilation database, and clang-tidy's `clang-diagnostic-*` reports them before a build.

**Hardened standard library** in every `build.sh` profile (dev and asan), for upstream and fork
code alike: `_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG` (libc++, the Mac) and
`_GLIBCXX_ASSERTIONS` (libstdc++, the container), from `tools/dev/hardening.py` through
`site-dev.py`, so container bounds and optional misuse trap at run time. Iterator dereferences
are not checked on the Mac: the SDK's libc++ (22.1.6) is built without a bounded-iterator ABI
(`_LIBCPP_ABI_BOUNDED_ITERATORS*` unset), so an iterator past the end of a `vector`, `span` or
`string` is dereferenced unchecked even in debug mode, and the ABI macros are not ours to flip
(spike notes `tooling-capabilities.md`, section 5). `_GLIBCXX_ASSERTIONS` does not check
iterators either. Checked iterators come from libstdc++'s debug mode, `_GLIBCXX_DEBUG`, in the
nightly unit-test build in the container (#59); until then, only a dereference outside the
allocation is caught, by ASan in the check tier. On macOS a
libc++ trap would end in `abort()`, which leaves a crash report; `tools/dev/hardening-exit.h`,
forced into every compile by the same flags, defines libc++'s `_LIBCPP_VERBOSE_ABORT` hook to
print the message and exit 1, without touching `SConstruct`. Every step of `check.sh` that runs
built programs runs them under `tools/dev/no-window.sh` as well, which fails a run that leaves a
crash report.

**GPU validation is a gate**: every SDL_GPU run in a check runs under `tools/dev/gpu-validate.sh`,
with SDL's GPU debug mode (the program creates its device with `debug_mode` true), Metal API
validation (`MTL_DEBUG_LAYER=1`), Metal shader validation (Level 2 below) and the Vulkan
validation layer (`VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn,perf`). Neither SDL nor the layer
changes the exit status (spike #55, K4), so the wrapper scans the output and fails on any
validation message, and on a run whose validation was not active. Until the fork has GPU code
(#24), headless probes in the gate self-test prove it on Metal natively and on lavapipe in the
container.
`check.sh`'s `gpu_gate` step (`tools/dev/gpu_debug_mode.py`) scans every C, C++ and
Objective-C(++) source outside `third_party/`, game code included, with comments stripped: a
device is created as `SDL_CreateGPUDevice(formats, DEBUG, name)`, or by
`SDL_CreateGPUDeviceWithProperties` in a function that also sets
`SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN` to `DEBUG` with `SDL_SetBooleanProperty`, where
`DEBUG` is `true` or the constant `gpu_debug_mode` (namespace-qualified or not), which a release
build may define `false` to turn validation off. Any other `SDL_CreateGPUDevice*` fails as
unknown. The step also fails on a GPU program `check.sh` runs outside the wrapper; its `gpu_tests` step runs every program
`tools/dev/gpu-programs` registers through `gpu-validate.sh`.

**The check tier's budget** is 10 minutes per leg, measured on 2026-10-21 (M1 Max): the container
leg of `ci.sh --clean` took 307 s (shell tests 223 s, unit tests 34 s, sanitizer build and tests
35 s, under amd64 emulation), the native leg 154 s on a warm build (shell tests 122 s, leaks 23 s).
`check.sh` prints each step's time and fails the full run as `budget` when it takes longer.

**The rtrace tests** are #19's to provide: it must add `tools/dev/rtrace-tests.sh`, which takes
the build profile and a wrapper command (`rtrace-tests.sh asan no-window.sh`, `rtrace-tests.sh dev
no-window.sh leaks.sh`) and runs every rtrace test built by that profile through it. Until then
`check.sh`'s sanitizer and leaks steps print a SKIP naming that script.

**Justified exceptions** are inline only, `// NOLINT(check): reason` or
`// NOLINTNEXTLINE(check): reason`; `format.sh` fails on a NOLINT without a check list and a
reason.

**Ratchet.** The enabled set is the one that is clean, with zero false alarms, on the
representative fork file `tools/dev/tests/gates/clean.cpp.in` (the conventions' idioms). Each
tightening is its own small commit that leaves the tree green and updates the level below.

- **Level 1**: `clang-diagnostic-*`, `clang-analyzer-*`, `bugprone-*` (with
  `bugprone-use-after-move`) except `bugprone-easily-swappable-parameters`,
  `cppcoreguidelines-owning-memory`, `cppcoreguidelines-pro-type-*` except
  `cppcoreguidelines-pro-type-vararg`, `cppcoreguidelines-narrowing-conversions`,
  `modernize-use-using`, `modernize-use-nullptr`,
  `modernize-use-override`, `readability-identifier-naming` (lower snake case),
  `misc-const-correctness`; `WarningsAsErrors: '*'`; strict warnings as above. On LLVM 23.1.3
  these globs enabled 252 checks. `tools/dev/style/clang-tidy-checks` lists the active level's,
  and `gates_test.sh` fails when it and the config differ, so an LLVM upgrade that adds or moves
  a check under a glob changes the list in a reviewed commit. LLVM 23 moved the `hicpp` checks into
  `bugprone`: `bugprone-unhandled-code-paths` (a switch on a non-enum without `default`) stays
  on with its noisy missing-`else` mode off, and `bugprone-signed-bitwise` stays on; both
  options are set in the config, and a planted file proves each.
- **Level 2 (active)**: level 1, plus, from the spike notes `tooling-capabilities.md` (0 hits
  on `clean.cpp.in` each): `cppcoreguidelines-use-enum-class`,
  `cppcoreguidelines-avoid-c-arrays`, `cppcoreguidelines-pro-bounds-*` (pointer arithmetic and
  array-to-pointer decay), `cppcoreguidelines-no-malloc` and
  `cppcoreguidelines-special-member-functions` (a sole defaulted destructor allowed, the
  polymorphic base idiom), each proven by a planted file. Its 259 checks, the custom one
  below among them, are in `clang-tidy-checks`. Left out of `pro-bounds-*` for false alarms:
  `constant-array-index` and `avoid-unchecked-container-access` flag every subscript with a
  run-time index, into `std::array` and `std::span` too, which the hardened library traps;
  `clean.cpp.in` holds one (`x_at`).
  The config's `ExtraArgs` add LLVM 23's `-Wlifetime-safety-permissive` (a pointer or view used
  after its owner dies), which `clang-diagnostic-*` reports, proven by `dangling_view.cpp.in`;
  Apple clang 21 accepts the flag but reports nothing, so it is not a build flag. clangd reads
  `ExtraArgs` too, so the editor tier shows it.
  `ExtraArgs` also add `-Wunsafe-buffer-usage` (pointer arithmetic, raw subscripts and
  pointer-plus-length constructions), proven by `unsafe_buffer.cpp.in`; code on `std::span` and
  `std::array` stays clean, range-for and subscript alike, and the pixel wrapper, which builds its
  span from SDL's pointer and size, carries a second NOLINT. Its libc-call part
  (`-Wunsafe-buffer-usage-in-libc-call`) is off until `tools/dev/hardening-exit.h`'s `vdprintf`,
  libc++'s printf-shaped abort hook, has its NOLINT. A query-based custom check (LLVM 23
  `CustomChecks`, experimental: `format.sh` passes `--experimental-custom-checks`),
  `custom-void-pointer-cast`, fails any cast from `void *` (`static_cast`, `reinterpret_cast`,
  C-style) outside the `sdl_wrap*` files, proven red by `void_cast.cpp.in` and clean by
  `void_cast_wrapper.cpp.in`, the same casts staged as `src/sdl_wrap.cpp`; that file now holds
  the userdata and pixel wrappers `clean.cpp.in` had. clangd does not run custom checks, so it
  is a pre-commit check.
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
- **Level 2, run-time half (active)**: in the asan profile (`tools/dev/site-asan.py`), UBSan's
  Clang-only checks, which `undefined` leaves out, each fatal: `local-bounds` (through the UBSan
  runtime, `-fno-sanitize-trap`, since a trap leaves a crash report on macOS) and `nullability`
  on every file, `implicit-conversion` on fork files only (`fork_warnings.py`), because
  upstream's unit tests convert -1 to unsigned on purpose. site-asan.py adds each where `g++`
  has it: Apple clang on the Mac; GCC 14 in the container has none, so the build prints a note
  and the gate `ubsanclang` is a SKIP there. In `gpu-validate.sh`, Metal shader validation
  (`MTL_SHADER_VALIDATION=1`, reporting to stderr, `MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1`,
  not to os_log): its reports name the shader function they stopped ("executing kernel
  function"), and a Metal run without "Metal GPU Validation Enabled" fails. The gate `gpumetal`
  (macOS only) proves it with a compute shader writing past its buffer; on the probe it costs
  nothing measurable (1.3 s either way).

**Gate self-test.** `tools/dev/tests/gates_test.sh` copies each planted file in
`tools/dev/tests/gates/` into a throwaway repository and asserts that every gate the file names
goes red with the expected diagnostic, that `clean.cpp.in` passes them all, and that every entry of
the clang-tidy config's `Checks` (17 patterns at level 2) is named by some planted file's expected
diagnostic. That proves each pattern is on, not each of the checks: the single checks are planted
one by one, but each glob by one check under it (`bugprone-*` by four: narrowing-conversions,
signed-bitwise, unhandled-code-paths, use-after-move; `clang-analyzer-*` by `unix.Malloc`;
`clang-diagnostic-*` by `shadow`; `cppcoreguidelines-pro-type-*` by `static-cast-downcast`;
`cppcoreguidelines-pro-bounds-*` by `pointer-arithmetic`). That the rest of each glob is enabled is
what the committed `clang-tidy-checks` list and its test show; that they fire is upstream's to
test. A gate a later ticket owns shows as a SKIP naming that ticket until it defines the gate's
runner.

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
