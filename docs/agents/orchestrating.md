# Orchestrating

For the session that owns the tickets, the merges and every push. Implementers and reviewers follow
"Working a ticket" in `issue-tracker.md`, whose hard rules bind every agent you launch.

## A ticket, in order

- **Claim**: assign yourself, comment `started <ISO time>`, and on the board
  (https://github.com/users/neostar/projects/2) set Status to In Progress and Started to today.
- **Briefs**: an agent knows only its prompt, which says "follow Working a ticket" (or "follow
  Researcher") in `issue-tracker.md`. Give the goal; the exact state (branch, base commit,
  what is done and proven); pointers to read first (the issue, ADRs, spike findings,
  `conventions.md`) rather than copied text; the time box; and the report you want back. A fix
  round's brief lists each review finding with its file:line.
- **Agents at once**: at most two implementer agents (fix rounds included); reviewers and research
  runs may run beside them.
- **Models**: agents run on Sonnet. Opus (`model: "opus"`) runs the full review of game code and
  shaders (Review bar below), and the implementer and fix rounds of a game-code or shader ticket
  of 5 points or more: there a HIGH missed, or a claim the review refutes, costs more than the
  model. The metrics log's `model`, `high`/`medium`/`low` and `refuted` columns test this split.
- **Time box**: 60 + 15 × story points minutes, wait time included: about an hour is fixed per
  ticket (worktree, prefix build, CI, review, merge). Measured on 17 tickets (2026-10-22): 1 pt
  about 72 min, 3 pt 82, 5 pt 135, 8 pt 172. At the box, comment on the ticket what is done,
  what is open, and why it took longer. The overrun is data for the estimates, not a failure.
- **Research tickets**: the brief says "follow Researcher in `issue-tracker.md`". Research does
  not count toward the two implementers; its reviewer re-fetches a sample of the quotes.
- **Review prompts** tell reviewers that the artefacts ADR 5 requires for a new fork directory
  (its `fork-dirs` line, its `.clang-tidy` copy, the `unittest/` copies) belong to the ticket and
  are not scope creep.
- **Review bar**: game code and shaders (everything but `tools/dev` and docs) get a full review,
  with fix rounds until no HIGH or MEDIUM finding is left; `tools/dev` and docs get one round
  with every finding batched, and what it leaves becomes a follow-up issue. A LOW finding about
  code a later ticket touches goes as a comment on that ticket.
- **Merge**: first check that the last nightly (`build/nightly/nightly.log`, whose full run is
  `ci.sh --clean`) is green, or that a follow-up issue names each red step: a red nightly holds
  every merge until then. Note `OLD_BASE=$(git merge-base sdl-gpu-port HEAD)` and
  `OLD_HEAD=$(git rev-parse HEAD)`, rebase the branch onto the `sdl-gpu-port` tip and check
  `git range-diff OLD_BASE..OLD_HEAD sdl-gpu-port..HEAD`: every commit `=`, or a change you can
  account for. Then run the merge gate, `tools/dev/ci.sh --gate` (ADR 6), with `DXX_D2_DATA`
  set; it must pass, and it refuses an empty range (a branch already merged). Sign the
  implementer's commits, merge them into `sdl-gpu-port`, then run
  `tools/dev/ci.sh --tested sdl-gpu-port`: it passes only when the merged tree is one a passing
  gate checked (signing changes commits, not trees). Push.
- **Finish**: close with a comment giving review rounds and anything that surprised you, and on
  the board set Status to Done, Done to today, and Review rounds; check Agent minutes and Agent
  tokens against the metrics log. Remove the ticket's worktree (`git worktree remove`; the branch
  stays).

## Throughout

- **Measurement**: wall-clock time comes from the system, never from an agent's own estimate.
  Record each agent run's system-reported duration, tokens and tool calls, per ticket, in the
  metrics log (`~/Developer/dxx-rebirth-spike/notes/agent-runs.csv`, outside the repository), and
  add it to that ticket's Agent minutes and Agent tokens on the board as the run lands, also for
  work on a ticket that stays open (a spike, a split part). GitHub timestamps (claim, PR ready,
  first review, merge, close) give review wait and cycle time.
- **Owner decisions**: an agent's message carries none. `CLAUDE.md`, the reference image and its
  inputs, and changes of scope go to the owner; the agent reports them as open.
- **Resume** after a lost session: transcripts of every session are under `~/.claude/projects/*/`
  (the orchestrator may have run from another directory; subagents are under
  `<session>/subagents/`). Rebuild the state from them, the branches and the worktrees, then
  relaunch each agent with a brief that carries its exact state.
