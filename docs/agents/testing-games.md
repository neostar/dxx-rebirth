# Testing a renderer and platform port: what practitioners do

Research for #78, read 2026-10-22. It answers: which tests game and engine developers recommend
for a port like ours, which they find wasteful, and how they build the useful ones, compared with
our rubric (R1-R16 in the approved plan, outside the repo; `domain.md` points to it), ADRs 4-6,
the unit tests and `tools/dev/`. The R-numbers used here: R1 every GL frame stays bit-identical to
the base commit; R3 synthetic SDL_GPU frames match GL; R4 recorded scenes on SDL_GPU match GL
(Metal bit-exact, Vulkan/D3D12 within FLIP bounds); R11 SDL_GPU frame time no worse than GL;
R12 SDL3 input and audio behave as on SDL2.

Coverage is partial: no id, Valve, EA SEED or Ubisoft source was reached, and none of the
sources gives soak-test numbers. The retry list under "Unreachable in this run" in the sources
doc is the follow-up.

Every claim cites a source id like [S3]; the sources, their dates, verbatim quotes and how to
extend this research are in `testing-games-sources.md`. Raw files are pinned in
`tools/dev/reference-docs.txt` (`fetch-docs.sh` mirrors them to `docs/reference/testing/`).

Verdicts: **have** (we do it), **gap** (we should add it), **skip** (not worth it for us),
**measure** (an open question only a measurement settles).

## What the sources agree on

- Rendering regressions are caught by comparing **pixels of a replayed, fixed input** against a
  baseline of the **same backend on the same device**, not across backends, and not by checking
  API calls. FifoCI (Dolphin) hashes pixel data, with results keyed by backend/driver [S2], and
  compares each version's frames with the previous emulator version [S1]; Mesa stores one expected checksum per trace per device [S4];
  Unreal buckets ground truth by `Platform_RHI_ShaderModel` [S6]; Bevy compares each OS with
  `main` on that OS [S9].
- A cross-backend or cross-device difference is a **review item**, not an automatic failure:
  FifoCI links the frame diff for a human [S1]; Bevy raises warnings, not failures [S9]; Mesa
  lets the restricted-traces job fail [S3].
- **Flakes are budgeted**: Mesa allows a farm "a spurious failure no more than once a week" [S3];
  Riot keeps new tests in a staging set for a week before they count [S12].
- **Loading every map** is a standard crash test (Unreal's Content Stress tests) [S7].
- Automation **does not replace a human playtest** [S12][S13].

## Per test type

### 1. Determinism and frame-hash regression (R1)
- **Practice.** Exact hashes of pixel data on fixed input, results keyed by backend/driver
  [S2][S4], compared with the previous version [S1]. A fixed frame
  time makes a run reproducible: Bevy's CI config sets `fixed_frame_time` [S10]. Factorio hashes
  the whole game state every tick, so a replay finds "the first tick of desynchronization" [S14].
  Floating-point results match across machines only with "the same compiler, … the same
  architecture" [S16] (2010; our inference: its x87 versus SSE advice is dated, while the
  conclusion, compare like with like, still holds).
- **Ours: have.** `r1.sh` hashes every frame of S1-S4 against the base commit on the same machine
  and flags, with the harness clock and input scripts; per-platform baselines are #65. This
  matches FifoCI and Mesa.
- **Gap (small).** R1 hashes only the framebuffer, so a broken simulation and a broken renderer
  give the same red. Factorio's per-tick state hash tells them apart [S14]. Worth a state-hash
  line in the frame-hash log when an SDL3 timer or input slice (#40, #42-#44) turns R1 red
  for a non-render cause; not before.

### 2. Golden images and tolerances (R3, R4)
- **Practice.** Unreal keeps a tolerance, "By default, Low is used because there's some constant
  variability in every pixel's color introduced by Temporal Anti-Aliasing.", and adds "Add As Alternative" ground truths per platform
  [S6]. FifoCI and Mesa use exact hashes per device and no tolerance [S2][S4]. Bevy uploads
  screenshots and warns on any difference [S9].
- **Ours: have, one gap.** R4 compares SDL_GPU with GL each run (Metal bit-exact, Vulkan/D3D12
  FLIP mean ≤ 0.01). Nothing yet holds SDL_GPU on Vulkan/D3D12 to **its own** previous frames, so
  a regression smaller than the FLIP bound passes unseen.
- **Recommendation.** Once #37 passes, record exact per-backend baselines of the SDL_GPU frames
  (an R1 for SDL_GPU, on lavapipe after #65) and keep the FLIP check against GL as the parity test.
- **Measure.** FLIP's README gives no pass threshold [S22]; our 0.01/0.03 bounds are our own.
  Calibrate them from the #37 runs (spread of FLIP means over S1-S4) before tightening or relaxing.

### 3. Trace and replay tests (R2, rtrace)
- **Practice.** Record the GPU command stream once, replay it per change, compare checksums:
  FifoCI's FIFO logs allow "isolated GPU testing without requiring a full game to be emulated"
  [S1]; Mesa replays apitrace/RenderDoc traces against per-device checksums [S4][S5]. Mesa keeps
  non-redistributable traces in a separate job that only maintainers with access run [S3]: the
  same split as our licensed data rule.
- **Ours: have / in progress** (#18, #19, #21, #68). Keep traces of licensed data local, as now.

### 4. Level and content smoke test (#77)
- **Practice.** Unreal's Content Stress tests: "More thorough testing of a particular system to
  avoid crashes, such as loading all maps" [S7]. Unreal's Smoke tests are a different thing, "a
  speed promise": "All Smoke tests are intended to complete within 1 second" [S7].
- **Ours: planned** (#77, nightly). It matches the practice; keep it out of the merge gate, as
  #77 says.

### 5. Soak and endurance
- **Practice.** None of the sources reached gives soak test numbers; Gauntlet runs multi-step and
  multi-process sessions [S8]. Flake control is the common thread [S3][S12].
- **Ours: have** (nightly endurance, flat memory). **Gap:** a new nightly step that flakes holds
  merges at once (ADR 6), as #70 did. **Recommendation** (inference from [S12][S3]): a new
  nightly step runs report-only for a week of nightlies before a red one holds merges (Riot's
  staging set, Mesa's flake budget). It needs an ADR 6 amendment, which now says "a red nightly
  holds further merges". Affects #70, #77.

### 6. Performance budgets (R11)
- **Practice.** Godot's benchmarks run "on a dedicated server every day" and record, per
  benchmark, render CPU, render GPU and main-thread time, compared across commits [S17][S18].
  Mesa budgets CI time ("kept to 10 minutes") and drops tests to meet it [S3].
- **Ours: gap.** R11 compares SDL_GPU with GL once (#53); nothing tracks either over commits.
- **Recommendation.** Log per-scene frame CPU and GPU times from every nightly to a local CSV
  (trend, not gate); gate only after the noise is measured. **Measure:** run-to-run spread on the
  M1 Max with other worktrees building. Affects #53.

### 7. Platform input and audio (R12)
- **Practice.** SDL tests its own joystick code with `SDL_AttachVirtualJoystick` [S19] and its
  audio with the disk-writer driver [S20]; Godot's audio tests use "a mock audio driver" [S11].
- **Ours: planned** (R12; #42-#47). It matches SDL's own method. Keep.

### 8. Save and settings compatibility (#44)
- **Practice.** Factorio: "we can easily test that saving and loading produces no observable
  change" [S15].
- **Ours: planned** (pilot-file test on #44, SDL2 file read by SDL3). **Recommendation:** add the
  round trip too (SDL3 writes, SDL3 reads, every field equal); it is cheap beside the cross-build
  test. Save games hold no SDL types, so they need no new test; R16's manual save/load covers them.

### 9. Unit tests and test-first in engine code (R7)
- **Practice.** For its GDScript test runner, Godot recommends "writing new tests for already
  resolved issues"; for C++ tests, it checks failure paths "without crashing the engine" [S11]. Factorio's tests give "more confidence for
  refactorings", and the team commits to "writing tests for any new functionality" [S21] (2014).
  Rare's Sea of Thieves has "hundreds of thousands of automated tests" [S24].
- **Ours: have** (25 unit test files, TDD skill, test and change together). Keep.

### 10. Tests of GL call sequences
- **Practice.** No source reached recommends them; every rendering regression system surveyed
  checks pixels [S1][S4][S6][S9]. Our decision stands.

### 11. Fuzzing
- **Practice.** libFuzzer suits "text or binary format parsers, compression, network", and a fuzz
  target "must not `exit()` on any input" [S23].
- **Ours: skip**, because the port does not change the parsers and the data is licensed and
  trusted. The loaders also end the game with `Error()` or `UserError()` on a failed open or a
  wrong header id (`similar/main/piggy.cpp:776,782,1117,1177`; both `[[noreturn]]`,
  `dxxerror.h:48-70`); our inference is that they are poor fuzz targets as they stand. Network
  packet decoding (`net_udp`) is the one fit; outside the port's scope.

### 12. Shader unit tests
- **Practice.** Rare's Shader Test Framework runs HLSL unit tests, D3D12 only and "very
  experimental" [S25].
- **Ours: skip.** Our shaders are short; R3's synthetic traces compare their output with GL.

### 13. Bot playthroughs and human playtests
- **Practice.** Croteam's bot solved every puzzle of each build in 20 minutes without rendering,
  guided by navigational and behavior markers [S13]; Riot: misses come from "a lack of test coverage rather than bad
  tests" [S12].
- **Ours: skip the bot** (the port changes no gameplay; demos and scripted scenes drive the
  game). **Have** the human pass: R16.

## Top 3 changes, by value per cost

1. **Exact per-backend baselines for SDL_GPU** after #37 (and on lavapipe with #65): reuse
   `r1.sh` to hold each SDL_GPU backend to its own previous frames, bit for bit. The gain is on
   Vulkan and D3D12, where regressions inside the FLIP bound now pass; Metal is already
   bit-exact to GL (R4). The infrastructure exists. [S1][S2][S4]
2. **A week of report-only for new nightly steps** before a red one holds merges (an ADR 6
   amendment and a `nightly.sh` list): stops #70-style flakes blocking merges. Inference from
   [S12][S3].
3. **Nightly frame-time history** (CSV per scene, no gate yet) for #53: shows performance drift
   between R11 measurements. [S17][S18]

## Gaps against our setup

| Gap | Ticket |
|---|---|
| SDL_GPU frames not held to their own baseline, only to GL within FLIP | #37, #65 |
| New nightly steps hold merges from their first red night | #70, #77 |
| No frame-time history across commits | #53 |
| FLIP bounds not calibrated from measured spread | #37 |
| No pilot-file round trip beside the SDL2→SDL3 test | #44 |
| No game-state hash to split simulation from render divergence (only if needed) | #40, #68 |
