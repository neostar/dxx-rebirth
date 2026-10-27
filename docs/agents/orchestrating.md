# Orchestrating

For the session that owns the tickets, the merges and every push. Implementers and reviewers follow
"Working a ticket" in `issue-tracker.md`, whose hard rules bind every agent you launch.

The goal is the SDL3 port on Metal, Vulkan and D3D12 through SDL_GPU: each choice serves its
critical path. Board and metrics operations go through `~/Developer/dxx-rebirth-spike/notes/board.sh` (outside the
repository; its header lists them).

## A ticket, in order

- **Choose**: `board.sh ready` lists the open tickets whose native blockers (GitHub issue
  dependencies, `issue-tracker.md`) are all closed. Take the one on the port's critical path.
- **Claim**: `board.sh claim N`.
- **Briefs**: an agent knows only its prompt, which says "follow Working a ticket" (or "follow
  Researcher") in `issue-tracker.md`. Give the goal; the exact state (branch, base commit,
  what is done and proven); pointers to read first (the issue, ADRs, spike findings,
  `conventions.md`) rather than copied text; the time box; and the report you want back. A fix
  round's brief lists each review finding with its file:line. Ground it in its lane: name the
  docs "Grounding a brief" in `domain.md` lists, and ask the report to say which it opened.
- **Agents at once**: at most two implementer agents (fix rounds included); reviewers and research
  runs may run beside them.
- **Models**: agents run on Sonnet. Opus (`model: "opus"`) runs the full review of game code and
  shaders (Review bar below), and the implementer and fix rounds of a game-code or shader ticket
  of 5 points or more: there a HIGH missed, or a claim the review refutes, costs more than the
  model. The metrics log's `model`, `high`/`medium`/`low` and `refuted` columns test this split.
- **Time box**: 60 + 15 × story points agent minutes, the sum of a ticket's implement, review
  and fix runs (the board's Agent minutes). On 32 tickets (2026-10-27) the medians fit it, with a
  wide spread per size (5 pt: 44 to 367). Claim to close takes about 3.5 times the box: gates
  run alone, signing waits for the owner, two implementers at most. So forecast a date from
  throughput (points closed per working day), never from boxes. At the box, comment on the ticket
  what is done, what is open, and why it took longer. The overrun is data for the estimates, not a
  failure.
- **Research tickets**: the brief says "follow Researcher in `issue-tracker.md`". Research does
  not count toward the two implementers; its reviewer re-fetches a sample of the quotes.
- **Review prompts** tell reviewers that the artefacts ADR 5 requires for a new fork directory
  (its `fork-dirs` line, its `.clang-tidy` copy, the `unittest/` copies) belong to the ticket and
  are not scope creep, and that a `Large-commit:` trailer stands only when they find no split
  in which each commit builds; otherwise the review proposes that split. Each review ends with
  `SEVERITY high=N medium=N low=N refuted=N`, refuted counting the implementer's claims it
  disproved, for the metrics log.
- **Review bar**: game code and shaders (everything but `tools/dev` and docs) get a full review,
  with fix rounds until no HIGH or MEDIUM finding is left; `tools/dev` and docs get one round
  with every finding batched. A finding left after that goes as a comment on the open ticket
  that next touches the code; a new issue only when it blocks the port.
- **Simplify**: for a game-code or shader ticket, once review leaves no HIGH or MEDIUM and the
  ticket's checks pass, run `/simplify` once on the branch diff (`sdl-gpu-port..HEAD`). Apply a
  finding when it removes code or per-frame cost in fork-owned code that outlives the ticket.
  Leave it, with a one-line reason in the closing comment, when it reaches upstream lines the
  branch does not already change (ADR 1: each one costs a rebase), scaffolding a named ticket
  removes, tooling, or a debug-only path. A hot-path cost goes to the frame-time comparison
  (base against branch, alone on the machine), not to an edit. Applied fixes land as one cleanup
  commit before the gate. Done when every finding is applied or left with its reason.
- **Merge**: first check that the last nightly (`build/nightly/nightly.log`, whose full run is
  `ci.sh --clean`) is green, or that a follow-up issue names each red step: a red nightly holds
  every merge until then. Note `OLD_BASE=$(git merge-base sdl-gpu-port HEAD)` and
  `OLD_HEAD=$(git rev-parse HEAD)`, rebase the branch onto the `sdl-gpu-port` tip and check
  `git range-diff OLD_BASE..OLD_HEAD sdl-gpu-port..HEAD`: every commit `=`, or a change you can
  account for. Then run the merge gate, `tools/dev/ci.sh --gate` (ADR 6), with `DXX_D2_DATA`
  set, alone on the machine (no implementer or building reviewer: parallel builds push its legs
  over budget); it must pass, and it refuses an empty range (a branch already merged). Before
  signing, sign a throwaway object (`git commit-tree -S HEAD^{tree} </dev/null`): a locked
  1Password fails there instead of halfway through a rebase, and signing waits for the owner. Sign the
  implementer's commits, merge them into `sdl-gpu-port`, then run
  `tools/dev/ci.sh --tested sdl-gpu-port`: it passes only when the merged tree is one a passing
  gate checked (signing changes commits, not trees). Push.
- **Batches** (owner 2026-10-27): a gate's cost is fixed per run (R1 baseline and capture,
  leaks, sanitizers, and the full run when any branch touches upstream game code), so gate
  what is reviewed and ready as one linear chain of 2-4 tickets when the machine frees: stack
  each branch on the one before, check each range-diff, run one `ci.sh --gate` on the chain's
  tip, and sign and merge the chain in one sitting with the owner. A ticket that unblocks
  another (a native dependency) goes in the next gate, alone if need be. When a batch fails,
  read which ticket the failing step names; split and re-gate only when the log can't tell.
- **Finish**: close with a comment giving review rounds and anything that surprised you, then
  `board.sh done N ROUNDS`. Remove the ticket's worktree (`git worktree remove`; the branch
  stays).

## Throughout

- **Measurement**: wall-clock time comes from the system, never from an agent's own estimate.
  As each run lands, `board.sh run` records its system-reported duration, tokens and tool calls
  (and a review's severity counts) in the metrics log and adds them to the ticket's board fields,
  also for work on a ticket that stays open (a spike, a split part). GitHub timestamps (claim, PR ready,
  first review, merge, close) give review wait and cycle time.
- **Owner decisions**: an agent's message carries none. `CLAUDE.md`, the reference image and its
  inputs, and changes of scope go to the owner; the agent reports them as open.
- **Resume** after a lost session: transcripts of every session are under `~/.claude/projects/*/`
  (the orchestrator may have run from another directory; subagents are under
  `<session>/subagents/`). Rebuild the state from them, the branches and the worktrees, then
  relaunch each agent with a brief that carries its exact state.
