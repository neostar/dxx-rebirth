# The port's test rubric (R1-R16)

The R-numbers that tickets, reviews and reports cite ("R1 identical", "R3 passes"). Each row is
the current criterion and how it is proven. Tickets and the board hold the work, the ADRs the
decisions; the original plan is archived, frozen, in `docs/history/2026-10-20-port-plan.md`.

| # | Criterion | Pass | Proof |
|---|---|---|---|
| R1 | Refactors don't change the GL picture | Every frame's hash bit-identical to the base commit, same machine and flags | `tools/dev/r1.sh` on S1-S4 at `-maxfps` 60 and 200; in the merge gate (ADR 6) |
| R2 | Trace fidelity | GL replay of a recorded trace equals the in-game GL frame (hash), every 30th frame | Open: #68 (after #18) |
| R3 | Synthetic SDL_GPU frames match GL | Metal: GL's pinned frame-hash lines, exactly. Vulkan and D3D12: hashes recorded, judged by FLIP in #37 | `tools/dev/rtrace-tests.sh` (`check_gpu`); D3D12: `tools/dev/rtrace-d3d12.sh` on WARP (ADR 4) |
| R4 | Scenes on SDL_GPU match GL | Metal bit-exact. Vulkan and D3D12: per scene, p95 frame FLIP mean <= 0.01, worst frame mean <= 0.03; no max-error gate | Open: #37. D3D12 frames come from the manual WARP run |
| R5 | Coverage | Every reachable (topology x blend x depth x alpha_ref x filter x wrap x MSAA x aniso x texture format) tuple hit; unreachable ones listed with a reason | Open: #20, #22 |
| R6 | Validation | 0 messages in every R3, R4 and R12 run | `tools/dev/gpu-validate.sh` (Metal, Vulkan); D3D12 debug layer with GPU-based validation via `rtrace-d3d12.sh` |
| R7 | Unit tests | All green on the sdl2 and sdl3 builds | `tools/dev/build.sh dev check`; in `check.sh` and the gate |
| R8 | Existing build matrix | SDL1.2+GL, SDL2+GL, SDL1.2 `opengl=0`, `opengles=1` (compile), Windows MinGW, macOS SDL2, `check_header_includes=1`; defaults unchanged | The local CI legs (ADR 4) and the nightly (ADR 5) |
| R9 | Shader blobs match sources | Byte-identical rebuild with the pinned shadercross | `tools/shaders/check.sh` (`check.sh`'s `shaders` step) |
| R10 | Lint, format, sanitizers | clang-format and clang-tidy clean on fork code, ASan and UBSan clean | `format.sh`, the sanitizer steps of `check.sh` (ADR 5) |
| R11 | Performance | SDL_GPU p50/p99 frame CPU time <= GL; GPU time <= GL + 10% | Open: #53. Device tickets: CPU per frame, base against branch, unchanged beyond the run spread (`~/Developer/dxx-rebirth-spike/notes/frametime.sh`, outside the repo) |
| R12 | SDL3 platform parity | Injected key, mouse and wheel events give the same DXX results as sdl2; `SDL_AttachVirtualJoystick` drives the `gc_*` handlers; disk-driver audio captures of SFX, music, ADLMIDI and MVE match sdl2 by RMS/peak envelope; the #877 saturation scan stays clean | Unit tests, `SDL_AUDIO_DRIVER=disk` scripts, `SDL_VIDEO_DRIVER=offscreen` smoke runs (#42-#48) |
| R13 | Retired | The renderer is chosen at startup, not switched in game (ADR 2) | None |
| R14 | Fallback | No usable GPU device (or only a software one): the game starts on GL and logs why. GL is the fallback until R16 passes on SDL_GPU, then the fork drops it | Open: #52 |
| R15 | Licensed data never committed | No hog, pig, dem, trace or golden made from licensed data in git | `check.sh`'s `guard` step (pre-commit), `docs/agents/game-data.md` |
| R16 | Fully playable (sdl3 + sdlgpu, macOS; R16w: the Windows build under CrossOver) | Owner's checklist: D1 and D2 menus, new game, save and load, demo, briefings with voice, every graphics option (#54), all movies, all music sources (SC-55, OPL3, MIDI, CD jukebox), gamepad, fullscreen toggle, Retina and Studio Display | The signed-off checklist, before release (#54) |

The scenes S1-S4 and how `r1.sh` plays them: `docs/agents/game-data.md`. S5 (renderer switch)
went with R13.
