# Issue tracker: GitHub

Issues and specs for this repo live as GitHub issues. Use the `gh` CLI for all operations.

## Conventions

- **Create an issue**: `gh issue create --title "..." --body "..."`. Use a heredoc for multi-line bodies.
- **Read an issue**: `gh issue view <number> --comments`, filtering comments by `jq` and also fetching labels.
- **List issues**: `gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'` with appropriate `--label` and `--state` filters.
- **Comment on an issue**: `gh issue comment <number> --body "..."`
- **Apply / remove labels**: `gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- **Close**: `gh issue close <number> --comment "..."`

**Always pass `-R neostar/dxx-rebirth`.** This clone has two remotes: `origin` is the fork and `upstream` is the official dxx-rebirth repo. Issues for this work live on the fork only; never create, comment on or label anything upstream.

## Working a ticket

The orchestrating session owns the ticket, the merge and every push; an implementer agent owns one
branch. A prompt that says "follow Working a ticket" adds only the ticket number and the time box.

**Orchestrator**

- **Claim**: assign yourself, comment `started <ISO time>`, and on the board
  (https://github.com/users/neostar/projects/2) set Status to In Progress and Started to today.
- **Agents at once**: at most two implementer agents (fix rounds included); reviewers may run
  beside them.
- **Time box**: 60 + 15 × story points minutes, wait time included. Calibrated on 17 tickets
  (2026-10-22): 1 pt took about 72 min, 3 pt 82, 5 pt 135, 8 pt 172; about an hour is fixed per
  ticket (worktree, prefix build, CI, review, merge). At the box, comment on the ticket what is
  done, what is open, and why it took longer. The overrun is data for the estimates, not a
  failure.
- **Review prompts** tell reviewers that the artefacts ADR 5 requires for a new fork directory
  (its `fork-dirs` line, its `.clang-tidy` copy, the `unittest/` copies) belong to the ticket and
  are not scope creep.
- **Merge**: first check that the last nightly (`build/nightly/nightly.log`, whose full run is
  `ci.sh --clean`) is green, or that a follow-up issue names each red step: a red nightly holds
  every merge until then. Then run the merge gate, `tools/dev/ci.sh --gate` (ADR 6), on the
  branch merged with the current `sdl-gpu-port` tip, with `DXX_D2_DATA` set; it must pass, and it
  refuses an empty range (a branch already merged). Sign the implementer's commits, merge them
  into `sdl-gpu-port`, then run `tools/dev/ci.sh --tested sdl-gpu-port`: it passes only when the
  merged tree is one a passing gate checked (signing changes commits, not trees). Push.
- **Finish**: close with a comment giving review rounds and anything that surprised you, and on
  the board set Status to Done, Done to today, and Review rounds; check Agent minutes and Agent
  tokens against the metrics log. Remove the ticket's worktree (`git worktree remove`; the branch
  stays).
- **Measurement**: wall-clock time comes from the system, never from an agent's own estimate. The
  orchestrating session records each agent run's system-reported duration, tokens and tool calls,
  per ticket, in the metrics log (`~/Developer/dxx-rebirth-spike/notes/agent-runs.csv`, outside
  the repository), and adds it to that ticket's Agent minutes and Agent tokens on
  the board as the run lands, also for work on a ticket that stays open (a spike, a split part);
  GitHub timestamps (claim, PR ready, first review, merge, close) give review wait and cycle time.

**Hard rules**, for implementers and reviewers; prompts point here instead of repeating them:

- Git: never `git stash` (one list for all worktrees); commit unsigned
  (`git -c commit.gpgsign=false commit`); no pushes, no issue or PR writes.
- Processes: kill only your own pids, never `pkill -f` with a broad pattern.
- Background runs: wait for one through `tools/dev/wait-for.sh FILE DEADLINE STRING...` on its
  log or output, naming every ending: `ci.sh: passed` and `ci.sh: failed` for `ci.sh`,
  `nightly.sh: passed` and `nightly.sh: failed` for `nightly.sh`. End all your background work
  before you report.
- Crash reports: create, change or delete nothing in `~/Library/Logs/DiagnosticReports`; on a
  new crash report, stop and report it.
- Windowless: launch binaries with `tools/dev/run.sh` and any other command that may open a window
  with `tools/dev/no-window.sh` (with `ASAN_OPTIONS=abort_on_error=0`, which `run.sh` sets). The
  game runs only hidden through `run.sh` (`game-data.md`): `DXX_DBG_HIDDEN=1`, a fresh
  `D2X_REBIRTH_HOME`, `-window`; never `-userdir`, never full screen.
- Silent: nothing plays sound. `no-window.sh` (and so `run.sh`) forces SDL's dummy audio driver;
  never set `NO_WINDOW_SOUND=1` and never launch the game around these scripts.
- Licensed data and everything derived from it (`game-data.md`) stay in your own scratch folder:
  never committed or quoted; report counts and pass/fail only. Leave other agents' scratch
  folders alone.
- Owner's OK first: the `dxx-rebirth-ref*` Docker images and the reference image inputs (`env.sh`,
  `deps.sh`, `tools/dev/container/Dockerfile`, `toolchain.pins`, `requirements.lock`).
- No environment dumps, no installs. On a denied permission, stop and report.
- Merge gate: `tools/dev/ci.sh --gate` (ADR 6) decides a merge, run on committed work with
  `DXX_D2_DATA` set (it fails without the data); its R1 outputs are licensed-data derived
  (`game-data.md`).

**Implementer**

1. **Clock**: run `date -u +%FT%TZ` as your first and last command.
2. **Worktree**: work in your own worktree on a new branch from `sdl-gpu-port`, and confirm the base
   (`git merge-base --is-ancestor sdl-gpu-port HEAD`) before the first edit.
3. **Commits**: in the Commits format of `conventions.md`, ending with your model's
   `Co-Authored-By: ... <noreply@anthropic.com>` trailer. The orchestrator signs them at merge.
4. **Plants**: for every check or guard you write, plant its removal once (delete it or make it
   pass everything) and run its test: the test must go red. Restore it before committing.
5. **Time box**: at the box, commit what is green and go to the report.
6. **Report**, once the merge gate, `tools/dev/ci.sh --gate` (ADR 6), passes on your branch merged
   with the current `sdl-gpu-port` tip; it adds the container leg or the full run when the
   changed paths need them. Start `ci.sh` and other long builds first, in the background, and
   work while they run. Give start and end times; commits by subject; each item as done, with the
   real run that proves it (the command on the real tools, its output, beside the tests with
   fakes), or open, with why; the plants, each with the test that went red; anything surprising.

**Reviewer**

- Follow the hard rules above.
- Check each implementer report for its plants, and each `tools/dev` script against the
  fail-closed checklist in `conventions.md`.

## Pull requests as a triage surface

**PRs as a request surface: no.** _(Set to `yes` if this repo treats external PRs as feature requests; `/triage` reads this flag.)_

When set to `yes`, PRs run through the same labels and states as issues, using the `gh pr` equivalents:

- **Read a PR**: `gh pr view <number> --comments` and `gh pr diff <number>` for the diff.
- **List external PRs for triage**: `gh pr list --state open --json number,title,body,labels,author,authorAssociation,comments` then keep only `authorAssociation` of `CONTRIBUTOR`, `FIRST_TIME_CONTRIBUTOR`, or `NONE` (drop `OWNER`/`MEMBER`/`COLLABORATOR`).
- **Comment / label / close**: `gh pr comment`, `gh pr edit --add-label`/`--remove-label`, `gh pr close`.

GitHub shares one number space across issues and PRs, so a bare `#42` may be either: resolve with `gh pr view 42` and fall back to `gh issue view 42`.

## When a skill says "publish to the issue tracker"

Create a GitHub issue.

## When a skill says "fetch the relevant ticket"

Run `gh issue view <number> --comments`.

## Wayfinding operations

Used by `/wayfinder`. The **map** is a single issue with **child** issues as tickets.

- **Map**: a single issue labelled `wayfinder:map`, holding the Notes / Decisions-so-far / Fog body. `gh issue create --label wayfinder:map`.
- **Child ticket**: an issue linked to the map as a GitHub sub-issue (`gh api` on the sub-issues endpoint). Where sub-issues aren't enabled, add the child to a task list in the map body and put `Part of #<map>` at the top of the child body. Labels: `wayfinder:<type>` (`research`/`prototype`/`grilling`/`task`). Once claimed, the ticket is assigned to the driving dev.
- **Blocking**: GitHub's **native issue dependencies**, the canonical, UI-visible representation. Add an edge with `gh api --method POST repos/<owner>/<repo>/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`, where `<blocker-db-id>` is the blocker's numeric **database id** (`gh api repos/<owner>/<repo>/issues/<n> --jq .id`, _not_ the `#number` or `node_id`). GitHub reports `issue_dependencies_summary.blocked_by` (open blockers only, the live gate). Where dependencies aren't available, fall back to a `Blocked by: #<n>, #<n>` line at the top of the child body. A ticket is unblocked when every blocker is closed.
- **Frontier query**: list the map's open children (`gh issue list --state open`, scoped to the map's sub-issues / task list), drop any with an open blocker (`issue_dependencies_summary.blocked_by > 0`, or an open issue in the `Blocked by` line) or an assignee; first in map order wins.
- **Claim**: `gh issue edit <n> --add-assignee @me`, the session's first write.
- **Resolve**: `gh issue comment <n> --body "<answer>"`, then `gh issue close <n>`, then append a context pointer (gist + link) to the map's Decisions-so-far.
