# Sources for `testing-games.md`

Every source was fetched in the #78 run on 2026-10-22. "Pinned" means the raw file is in
`tools/dev/reference-docs.txt` (block `# Testing research (#78)`) with its SHA-256, mirrored by
`fetch-docs.sh` to `docs/reference/testing/`; quotes from pinned files are copied from the file.
Quotes from HTML pages (unpinned) were extracted with a fetch tool asked for verbatim text; check
them against the page before relying on a word.

| Id | Source | Author | Stated date | Where |
|---|---|---|---|---|
| S1 | FifoCI "About" template | Pierre Bourdon (delroth), Dolphin | repo pushed 2026-10-09 | pinned `testing/fifoci-about-view.html`, dolphin-emu/fifoci@1cf03c90 |
| S2 | FifoCI runner `__main__.py` | Pierre Bourdon | © 2014, same commit | pinned `testing/fifoci-runner-main.py` |
| S3 | Mesa `docs/ci/index.rst` | Mesa CI maintainers | tag mesa-26.2.5 (2026-10-14) | pinned `testing/mesa-ci-index.rst` |
| S4 | Mesa llvmpipe `traces-llvmpipe.toml` | Mesa | mesa-26.2.5 | pinned `testing/mesa-traces-llvmpipe.toml` |
| S5 | Mesa `docs/ci/local-traces.rst` | Mesa | mesa-26.2.5 | pinned `testing/mesa-local-traces.rst` |
| S6 | Unreal "Screenshot Comparison Tool" | Epic Games | UE 5.8 docs, undated | dev.epicgames.com/documentation/en-us/unreal-engine/screenshot-comparison-tool-in-unreal-engine |
| S7 | Unreal "Automation Test Framework" | Epic Games | UE 5.8 docs | …/automation-test-framework-in-unreal-engine |
| S8 | Unreal "Gauntlet Automation Framework Overview" | Epic Games | UE 5.8 docs | …/gauntlet-automation-framework-overview-in-unreal-engine |
| S9 | Bevy `example-run.yml`, `send-screenshots-to-pixeleagle.yml` | Bevy | tag v0.20.0 (2026-10-08) | pinned `testing/bevy-example-run.yml`, `testing/bevy-send-screenshots-to-pixeleagle.yml` |
| S10 | Bevy `bevy_dev_tools/src/ci_testing/config.rs` | Bevy | v0.20.0 | pinned `testing/bevy-ci-testing-config.rs` |
| S11 | Godot "Unit testing" (`unit_testing.rst`) | Godot docs | Godot 4.7 `latest`; godot-docs@0914a4d4 | pinned `testing/godot-unit_testing.rst` |
| S12 | "Automated Testing for League of Legends" | Jim Merrill, Riot | 2016-02-26 | riotgames.com/en/news/automated-testing-league-legends |
| S13 | "How Croteam built a bot to beat … The Talos Principle" | Damjan Mravunac, Croteam | 2015-09-25 | blog.playstation.com/archive/2015/09/25/how-croteam-built-a-bot-to-beat-its-ps4-puzzler-the-talos-principle/ |
| S14 | Factorio Friday Facts #47 "CRC fun" | Tomas, Wube | 2014-08-15 | factorio.com/blog/post/fff-47 |
| S15 | Factorio Friday Facts #270 (save/load) | Rseding, Wube | 2018-11-23 | factorio.com/blog/post/fff-270 |
| S16 | "Floating Point Determinism" | Glenn Fiedler | 2010-02-24 | gafferongames.com/post/floating_point_determinism/ |
| S17 | Godot contributing: benchmarks | Godot | undated, `latest` | contributing.godotengine.org/en/latest/development/other/benchmarks.html |
| S18 | godot-benchmarks `README.md` | Godot | commit 2026-09-16 | pinned `testing/godot-benchmarks-README.md` |
| S19 | SDL `test/testautomation_joystick.c` | SDL | release-3.4.18 | pinned `testing/sdl-testautomation_joystick.c` |
| S20 | SDL `test/testautomation_audio.c` | SDL | release-3.4.18 | pinned `testing/sdl-testautomation_audio.c` |
| S21 | Factorio Friday Facts #60 "Tests all around" | Tomas, Wube | 2014-11-14 | factorio.com/blog/post/fff-60 |
| S22 | FLIP `README.md` | NVIDIA | commit pinned 2026-10-20 | already pinned `flip/README.md` |
| S23 | LLVM `LibFuzzer.md` | LLVM | llvmorg-23.1.3 (our pinned LLVM) | pinned `testing/LibFuzzer.md` |
| S24 | "Automated Testing of Shader Code", Pure Virtual C++ 2024 | Keith Stockdale, Rare | page updated 2024-05-02 | learn.microsoft.com/en-us/shows/pure-virtual-cpp-2024/automated-testing-of-shader-code |
| S25 | ShaderTestFramework `README.md` | Keith Stockdale | commit 06ae57f0 (2026-10) | pinned `testing/ShaderTestFramework-README.md` |

## Quotes

- S1: "Dolphin can record and replay data sent from the CPU to the GPU, allowing for isolated GPU
  testing without requiring a full game to be emulated." "Each Dolphin version is tested by
  rendering a chosen set of FIFO logs, dumping the frames, and comparing the dumped pixels data to
  previous versions of the emulator." "If a link is present in the cell, it means a difference
  was found compared to the previous Dolphin version. Clicking that link will show the difference
  frame by frame."
- S2: "From a given image file, generate a hash of the pixel data of that image." (`hashlib.sha1`
  of the RGB bytes); runner configs per backend: `Config-ogl`, `Config-sw`, `Config-vk`,
  `Config-mtl`, `Config-mvk`, `Config-dx-nv`. "HACK: Currently D3D on Windows inserts a black
  frame as the first frame of all recordings."
- S3: "we require that a given driver's test farm produces a spurious failure no more than once a
  week. If every driver had CI and failed once a week, we would be seeing someone's code getting
  blocked on a spurious failure daily, which is an unacceptable cost to the project." "this
  generally means that the test runtime as reported by deqp-runner should be kept to 10 minutes."
  "If a test farm is short the HW to provide these guarantees, consider dropping tests to reduce
  runtime." "The second job replays traces listed in
  ``src/<driver>/ci/restricted-traces-<driver>.yml`` and it is allowed to fail." "Restricted
  traces are those that have been made available to Mesa developers without a license to
  redistribute at will".
- S4: `devices = { llvmpipe = { checksum = "e695d7c6…" } }` per trace.
- S5: "For comparing checksums use: … ./replayer.py compare trace -d test path/name_of_trace.trace 0".
- S6: "By default, Low is used because there's some constant variability in every pixel's color."
  "Because we only bucket screenshots based on Platform_RHI_ShaderModel" (then "Add As
  Alternative"). "Should the latest screenshot not match it will cause a testing failure."
- S7: Smoke: "Smoke tests are just considered a speed promise by the implementer." "All Smoke
  tests are intended to complete within 1 second." Content Stress: "More thorough testing of a
  particular system to avoid crashes, such as loading all maps or loading and compiling all
  Blueprints."
- S8: "a framework to run sessions of projects in Unreal Engine that perform tests and validate
  results." "It is well suited to smoke tests that require several steps to execute."
- S9: "# also run when pushed to main to update reference screenshots"; on a difference:
  `echo "::warning title=$diff different screenshots on $os::…"`; compare request
  `{"os":"<equal>", "branch": "main"}`; "Disable audio through a patch. on github m1 runners,
  audio timeouts after 15 minutes".
- S10: "The amount of time in seconds between frame updates." (`fixed_frame_time`); "An event to
  send at a given frame, used for CI testing."
- S11: "it's important to check that those failure paths are indeed safe to execute without
  crashing the engine." "It also enables a mock rendering server" (`[SceneTree]`). "Initializes
  the AudioServer using a mock audio driver." (`[Audio]`). "We recommend writing new tests for
  already resolved issues related to GDScript at GitHub".
- S12: "Early test-writers made heavy use of sleeps, which led to a number of fragile tests"
  "There, tests must demonstrate stability for at least one week before being promoted." "Issues
  not caught by the BVS generally slip by due to a lack of test coverage rather than bad tests."
  "bugs discovered in automation get resolved eight times faster than the average bug."
- S13: "His purpose was to play through each new game build and solve all puzzles", "it took him
  only 20 minutes to go through entire game", "While there will always be a place for standard
  human testers in games".
- S14: "The game runs in a special mode when it makes a CRC from the whole map every tick." "We
  also know that this is the first tick of desynchronization (we make the check every tick)."
- S15: "One of the key parts of the save/load process in Factorio is that it must be
  deterministic." "we can easily test that saving and loading produces no observable change".
- S16: "It is possible to get deterministic results for floating calculations across multiple
  computers provided you use an executable built with the same compiler, run on machines with the
  same architecture, and perform some platform-specific tricks."
- S17: the benchmarks repository "is run on a dedicated server every day"; results at
  benchmarks.godotengine.org.
- S18: "run the scene for five seconds and log the average per-frame statistics"; "**Render
  CPU:** Average CPU time spent rendering each frame"; "**Render GPU:** Average GPU time spent per
  frame."
- S19: `device_id = SDL_AttachVirtualJoystick(&desc);` then `SDL_SetJoystickVirtualButton(joystick,
  SDL_GAMEPAD_BUTTON_SOUTH, true)`.
- S20: "Remove a possibly created file from SDL disk writer audio driver".
- S21: "it should help us avoid same bugs in the future and give us more confidence for
  refactorings"; "writing tests for any new functionality we add to the game"; after the
  integration tests "a crc check is made against the set of presaved crc values".
- S22: no pass threshold is stated in the README (checked by search for threshold/tolerance).
- S23: "It must tolerate any kind of input (empty, huge, malformed, etc)." "It must not `exit()`
  on any input." "Examples: regular expression matchers, text or binary format parsers,
  compression, network, crypto."
- S24: "The 'Sea of Thieves' project has hundreds of thousands of automated tests that exist to
  validate behavior and to ensure that bugs are kept to a minimum".
- S25: "Shader code is code. We should test it"; "It uses D3D12 and requires HLSL 202x"; "It is
  currently very experimental".

## Currency

- Old but not superseded: S14, S21 (2014), S13 (2015), S12 (2016), S15 (2018). The practices
  (state CRCs, staging flaky tests, bots) recur in the 2026 sources (S3, S9).
- Partly superseded: S16 (2010) argues around x87 versus SSE; on arm64 and x86-64 SSE2 that
  choice is gone, but its conclusion (same compiler, same architecture) is what R1 already assumes
  (baselines per machine and flags).
- Engine docs (S6-S8, S11, S17) are the `latest` or 5.8 pages on the fetch date; they carry no
  page dates.

## Unreachable in this run

- GDC Vault (Masella, "Automated Testing of Gameplay Features in 'Sea of Thieves'", GDC 2019):
  the Vault redirected to a maintenance page on 2026-10-22.
- Unreal Fest Europe 2019, Jessica Baker, "Automated Testing at Scale in Sea of Thieves": 403
  (Cloudflare challenge), so its slides were not reached.
- Dolphin blog "Making Developers More Productive" (2015): 403 (bot challenge) on every mirror;
  FifoCI's repository (S1, S2) stood in.
- Brian Provinciano, "Automated Testing and Instant Replays in Retro City Rampage" (GDC 2015):
  only a dead announcement link; the Vault was down.
- Unreal "Automated Perf Testing" page: returned an empty table of contents.

## How to extend this research

- Retry the GDC Vault talks above when the Vault is back; Masella's slides are the best source
  on test-first gameplay code and test coverage levels.
- Pin new raw files in `tools/dev/reference-docs.txt` at a tag or commit, under the
  `# Testing research (#78)` block, with `shasum -a 256`, and run
  `tools/dev/fetch-docs.sh --dest <scratch dir>` to check the line.
- For each new source add a row (id, author, stated date, where) and a quote here, and cite it by
  id in `testing-games.md`; flag any source whose practice a newer one replaces under Currency.
- Measurements this research left open are marked **measure** in `testing-games.md`.
