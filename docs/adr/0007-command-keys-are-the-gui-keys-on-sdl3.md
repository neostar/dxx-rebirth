# 7. The Command keys are the GUI keys on SDL3, on macOS only

Date: 2026-10-28. Status: accepted (owner decided Cmd-Q on 2026-10-28). Extends ADR 3.

## Context

The game's key table has two meta keys, `LCMD` and `RCMD` (`KEY_LMETA`, `KEY_RMETA`), whose
modifier `KEY_METAED` is `KEY_COMMAND` in the bindings (`KEY_MAC` cases in `gamecntl.cpp`).
On SDL2, `key.cpp` defines `SDLK_LMETA` and `SDLK_RMETA` as `SDLK_UNKNOWN` on every platform, so
neither Cmd on a Mac nor the Windows or Super key on Windows and Linux ever reached the table.
SDL3 defines `SDLK_LMETA` and `SDLK_RMETA` itself, as "Extended" keys (`0x20000004u`,
`0x20000005u`) that no common keyboard sends, so the table would have compiled against them and Cmd
would still do nothing. A Mac's Cmd keys are `SDL_SCANCODE_LGUI` and `RGUI`, keycodes `SDLK_LGUI`
and `SDLK_RGUI` (`docs/agents/sdl-input.md`, "Modifiers and the macOS Cmd key"); the Windows and
Super keys are the same two keys.

SDL's Cocoa backend sends every key event to SDL, and so to the game, before the application
menu's key equivalents run (`SDL3Application sendEvent:` calls `Cocoa_DispatchEvent`, then
`[super sendEvent:]`; `R/sdl3/src/video/cocoa/SDL_cocoaevents.m:63-103`). Cmd-Q, Cmd-H, Cmd-M
and Cmd-W therefore reach the game as `KEY_COMMAND` plus the key, and their menu action runs as well:
Cmd-Q calls `terminate:`, which posts `SDL_EVENT_QUIT` (`:55-58`); Cmd-H hides the app; Cmd-M
minimises the window; Cmd-W closes the window, and closing the last window posts `SDL_EVENT_QUIT`
too, because `SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE` defaults to true
(`src/events/SDL_windowevents.c:316-317`).

## Decision

- The SDL3 build behaves as the SDL2 build does on each platform, except that Cmd works. In an
  `#elif SDL_MAJOR_VERSION == 3` branch of `key.cpp` (ADR 3), under `__APPLE__`, `LCMD` is
  `SDLK_LGUI` and `RCMD` is `SDLK_RGUI`: Cmd is a modifier, like Ctrl and Alt, and sets
  `KEY_METAED` on the next key. On every other platform the two stay `SDLK_UNKNOWN`, as on SDL2:
  the Windows and Super keys reach no game key, so Super+P does not pause as Cmd+P does
  (`gamecntl.cpp:563`) and the Cmd+Shift+3 bindings stay out of their reach. The SDL2 mapping is
  unchanged. `common/sdl/unittest/key_events.cpp` pins both branches.
- A binding on Q, H, M or W with Cmd held fires and the menu action runs beside it.
- Cmd-Q in play keeps today's behaviour, by the owner's decision: it shows the game's quit dialog
  and does not quit directly. The menu action's `SDL_EVENT_QUIT` becomes `event_type::quit`, which
  opens the dialog. `sdl_quit_reaches_the_game_as_a_quit_event` (`common/sdl/unittest/event_poll.cpp`)
  pins only that upstream path for `SDL_EVENT_QUIT`; no test presses Cmd-Q, which needs the
  Cocoa menu.

## Consequences

- Bindings written with `KEY_COMMAND` start to work on SDL3 on macOS, as on SDL1.2 builds where
  the keys were real.
- A change that routes `SDL_EVENT_QUIT` around the quit dialog, or sets
  `SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE`, changes this decision and needs the owner.
- Mapping the GUI keys on Windows or Linux is a new decision for the owner: it needs a binding
  review first.
