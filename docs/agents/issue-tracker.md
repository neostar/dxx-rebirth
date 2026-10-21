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
- **Time box**: a ticket's story points set its expected time, and the box is twice that. At the
  box, comment on the ticket what is done, what is open, and why it took longer. The overrun is
  data for the estimates, not a failure.
- **Merge**: sign the implementer's commits, merge them into `sdl-gpu-port`, push.
- **Finish**: close with a comment giving review rounds and anything that surprised you, and on
  the board set Status to Done, Done to today, and Agent minutes, Agent tokens and Review rounds
  from the metrics log.
- **Measurement**: wall-clock time comes from the system, never from an agent's own estimate. The
  orchestrating session records each agent run's system-reported duration, tokens and tool calls,
  per ticket, in the metrics log; GitHub timestamps (claim, PR ready, first review, merge, close)
  give review wait and cycle time.

**Implementer**

1. **Clock**: run `date -u +%FT%TZ` as your first and last command.
2. **Worktree**: work in your own worktree on a new branch from `sdl-gpu-port`, and confirm the base
   (`git merge-base --is-ancestor sdl-gpu-port HEAD`) before the first edit.
3. **Commits**: unsigned (`git -c commit.gpgsign=false commit`), in the Commits format of
   `conventions.md`, ending with your model's `Co-Authored-By: ... <noreply@anthropic.com>`
   trailer. The orchestrator signs them at merge.
4. **Windowless**: launch binaries with `tools/dev/run.sh` and any other command that may open a
   window with `tools/dev/no-window.sh`.
5. **Local**: your output is commits on your branch and the report; pushes and issue writes belong
   to the orchestrator.
6. **Time box**: at the box, commit what is green and go to the report.
7. **Report**, once the full `tools/dev/ci.sh`, both legs, passes on your branch merged with the
   current `sdl-gpu-port` tip. Start `ci.sh` and other long builds first, in the background, and
   work while they run. Give start and end times; commits by subject; each item as done, with the
   real run that proves it (the command on the real tools, its output, beside the tests with
   fakes), or open, with why; anything surprising.

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
