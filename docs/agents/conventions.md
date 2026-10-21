# Coding conventions

Upstream has no style guide; these rules come from its code and history (counts and commits in the
evidence file `design/CONVENTIONS-draft.md` of the spike workspace). Each rule names an example:
when in doubt, open it and match it.

## Two kinds of code

Every line you touch is **upstream code** or **fork code**:

- **Upstream code** is any file that exists in `upstream/master`. Match the surrounding lines exactly,
  quirks included, and change only the lines your slice needs. The fork is rebased on upstream
  regularly, so every extra changed line is a conflict on every rebase. Keep old long lines,
  `#ifndef` guards and globals as they are.
- **Fork code** is a file this fork adds (render device, adapters, platform seam, tools). It follows
  every rule below, the **Adopt** idioms and the C++ safety rules ADR. Put new code in small new
  files rather than growing upstream's large ones (`similar/main/net_udp.cpp` has 6734 lines).

Done when every touched line follows the rules for its kind of code.

## Formatting

- Tabs; function and class braces on their own line (`common/unittest/enumerate.cpp:17-27`).
  `namespace dcx {` keeps its brace on the same line (`common/include/homing.h:12`).
- `if (` with a space; west const; `&` and `*` bind to the name: `const T &x`, `T *p`.
- Brace-initialise: `unsigned count{0};`, designated initialisers `vms_vector a{.x = 10, ...}`.
- `[[nodiscard]]` on its own line above the declaration (`common/include/homing.h:20`).
- `template <typename T>` on its own line, `requires(...)` on the next, one condition per line
  when long (`common/main/d_bit_enum.h:20-43`).
- Keep new lines under about 100 columns. Upstream code has no formatter. Fork code is formatted
  by `tools/dev/format.sh` (clang-format with `tools/dev/style/`, `--staged` on the diff in the
  pre-commit hook, `--tidy` for clang-tidy), only in the directories listed in
  `tools/dev/fork-dirs`: add a new fork directory there, with a copy of `tools/dev/style/clang-tidy`
  as its `.clang-tidy`.
- C++23 is required (`SConstruct:1229`): `static operator()`, explicit object parameters and
  concepts are in use (52f6389ee, ff8df741e).

## Names and files

- Functions, locals, types and enum members in lower snake_case (`g3_draw_poly`, `vms_vector`,
  `player_flag::cloaked`). Library wrappers keep the library prefix (`PHYSFS_File_deleter`).
- New enums are `enum class` with an explicit underlying type (`common/include/3d.h:62`); flag sets
  use `d_bit_enum.h` operators (cc4278005).
- Aliases with `using`.
- Namespace `dcx` for code identical in Descent 1 and 2, `dsx` for code compiled once per game; a
  file may alternate (`similar/main/ai.cpp:89,188`). Close the namespace before every `#include`
  and keep `dcx`/`dsx` unnested (`Documentation/TODO/namespace-dcx-dsx.markdown`).
- `common/` holds shared code, `similar/` code compiled once per game, `d1x-rebirth/` and
  `d2x-rebirth/` game-only code.
- Lowercase file names. `fwd-*.h` for forward declarations, `d_*.h` for generic helpers
  (`common/main/d_enumerate.h`), unit tests named after their unit (`common/unittest/homing.cpp`).

## Idioms (Adopt)

- Strong index types through `valptridx` (`common/include/fwd-valptridx.h:235`).
- `enumerated_array<T, N, Index>` or `std::array` for fixed collections; `std::span` and
  `std::size` for views and sizes (58061b5f4, 6e4b63e09).
- Ownership in the type: `std::unique_ptr`, `std::make_unique`, `RAIIdmem` (70c44f42c).
- State as parameters: `obj_delete(d_level_unique_object_state &, segment_array &, ...)`
  (`common/main/fwd-object.h:242`).
- Logging with `con_printf(CON_NORMAL, fmt, ...)`; fatal errors with `UserError(fmt, ...)`; both are
  format-checked (`common/include/dxxerror.h:39-75`).
- Exceptions only for invariant and range violations (`common/include/valptridx.h`); normal errors
  travel as return values.
- Constants as `constexpr` values or `enum class` members; standard library over custom shims.
- Testability: extract a pure kernel into a small header and test it (dbd8f0241, c5483860d).

## Headers and build options

- New headers: `#pragma once`, the DXX-Rebirth banner (`common/include/homing.h:1-6`), and they
  compile on their own (`check_header_includes`). Files derived from Parallax code keep the
  Parallax banner.
- Standard headers first, then project headers in quotes.
- Game-specific code: `#if DXX_BUILD_DESCENT == 1` / `#elif DXX_BUILD_DESCENT == 2`.
- A new build option is one `('name', default, 'help')` tuple in its `BoolVariable` or
  `EnumVariable` group (`SConstruct:3982-4060`), turned into a `DXX_USE_<NAME>` macro of 0 or 1 by a
  `_check_user_settings_<name>` test with `cpp_defines_with_condition_value`
  (`SConstruct:1504-1523`), and tested with `#if`. `-Wundef` then catches misspelt names.
- `NDEBUG` changes behaviour at 88 sites: run tests on debug and release builds.

## Comments

- `/* */` block comments for explanations, above the template or function they explain
  (`common/main/d_bit_enum.h:15-29`); no doxygen.

## Tests

- One Boost.Test module per file in `common/unittest/` (game-only: `d2x-rebirth/unittest/`):
  `BOOST_TEST_DYN_LINK`, `BOOST_TEST_MODULE Rebirth <name>`, `BOOST_AUTO_TEST_CASE(snake_case_name)`,
  asserting with `BOOST_TEST` (`common/unittest/enumerate.cpp:1-10`).
- Each case carries a block comment `/* Test that <behaviour>. */` directly above it
  (`enumerate.cpp:19`).
- Register new files in `runtime_test_boost_tests` (`SConstruct:4999`); run them with `check`.

## Shell scripts

`check.sh --fast` runs shellcheck on `tools/dev/fork-dirs`; these are the calls it cannot make:

- Throwaway repositories come from `make_repo` in `tools/dev/tests/lib.sh`, and scripts source
  `tools/dev/env.sh`, so git's repository variables never reach the real repository.
- Use options that behave the same in BSD and GNU tools (`od`, not `xxd`; no `sed -i`).
- Test each option's default path, not only its overrides.
- A missing tool is a visible SKIP naming the tool, never a silent pass.

## Commits

- Subject: `` `symbol_or_file`: lowercase imperative summary `` (67bf8de68, 3285a3de9); a short
  plain sentence when no single symbol owns the change (5ad7617e2).
- Body: short prose explaining why, with measured effects (67bf8de68 gives the size change in
  bytes); mechanical rewrites quote the exact command used (061c50b44).
- Trailers: `Fixes: <full hash> ("<old subject>")` for regressions, `Reported-by: <name> <issue URL>`
  for reported bugs.
- One concern per commit, at most about 300 changed lines, test and change together; style changes
  ride along with the change that needs them. The pre-commit hook enforces the 300, leaving out
  the generated files `tools/dev/commit-size-exclude` lists, counting a binary file as 100 lines
  and an amend as the whole amended commit; for a larger change that cannot be
  split, commit with `DXX_ALLOW_LARGE_COMMIT=<reason>`, which the hook prints, and give the reason
  in a `Large-commit: <reason>` trailer. git runs no hook for the commits a `rebase -i` squash or
  fixup makes, so `check.sh --fast` (`commit_sizes`) measures every fork commit against its first
  parent too, letting through only a `Large-commit:` trailer or a subject that
  `tools/dev/commit-size-allow` lists with a reason.
- Docs change in the same series as the code they describe.
- Commits are signed; push only signed commits.
- Cite fork commits by subject, never by hash: rebases and re-signing change every fork hash.
  `check.sh --fast` fails on a fork hash in `docs/`, `CLAUDE.md` or `GLOSSARY.md`; upstream
  hashes are fine.

## Legacy

Upstream is moving away from these. Leave existing instances as they are and write the modern form
in fork code:

| Legacy | Modern form |
|---|---|
| plain `enum`, `CON_*`-style constants, `#define` constants and flags | `enum class`, `d_bit_enum`, `constexpr` |
| `typedef` | `using` |
| `#ifndef _X_H` guards | `#pragma once` |
| `MALLOC` / `d_free` | `std::unique_ptr`, `RAIIdmem` |
| new mutable `Capitalised_snake` globals | state passed as parameters |
| explicit `*_close` / `free_*` teardown | RAII owners |
| `defined(DXX_BUILD_DESCENT_I)` | `DXX_BUILD_DESCENT == 1` |

## Quirks fork code works within

- `dsx` is a macro and `using namespace dcx/dsx` is injected by `dxxsconf.h`, so a wrong namespace
  can compile silently: check where each new symbol lands.
- 588 `extern` globals in `common/` headers: new code takes state as parameters and reads the
  globals only at its boundary.
- All build logic sits in the 6113-line `SConstruct`.

## Upstream candidates

Fixes to upstream code that upstream would want too are kept as separate, minimal commits so they
can be offered upstream, named by subject (see Commits):
`` `SConstruct`: discard make_unique results in configure tests `` and
`` `OPTIMIZER_HIDE_VARIABLE`: use a memory operand clang accepts on arm64 ``.

## Fork rules defined elsewhere

- C++ safety rules, the gates that enforce them and the active clang-tidy level:
  `docs/adr/0005-cpp-safety-rules-for-new-code.md` (ADR 5).
- Domain terms: `GLOSSARY.md`. Decisions: `docs/adr/` (soft fork, render device, SDL3 approach).
- Licensed game data, demos, captures and goldens derived from them stay out of git.
- Development tools run windowless and in the background (`SDL_HINT_MAC_BACKGROUND_APP`).
