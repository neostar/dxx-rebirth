# Upstream PRs

Rules for a PR to `dxx-rebirth/dxx-rebirth` (upstream; maintainer vLKp), which offers fork commits
kept as upstream candidates (`conventions.md`). Agents draft the PR text and review replies; the
owner posts them, since agents write nothing upstream (`issue-tracker.md`).

## Shape that was accepted

PR #878 (MVE audio sign extension) is the model; vLKp's verdict on it: "looks good as-is"
(https://github.com/dxx-rebirth/dxx-rebirth/pull/878#issuecomment-6484487242, 2026-10-29).

- One fix per PR, linked to its upstream issue (`Fixes #877`).
- Body sections `## Problem`, `## Change`, `## Validation`. Problem names the regressing commit
  and the measured symptom. Change states what the PR leaves untouched. Validation lists each
  check with numbers, the toolchains and platforms used, failures that also occur on unmodified
  master, and what was not tested.
- A known compromise goes in the body, with the reason and an offer to rearrange it.
- End with the AI assistance disclosure: what Claude did, what the owner did and verified.
- Two spaces after each sentence, in the body, replies and commit messages, as vLKp writes
  (185 of 193 sentence breaks in vLKp's last 200 upstream commits).

## vLKp's stated preferences

- **Build each source file once.** vLKp prefers no `.cpp` included into another source. #878's
  test includes `mve_audio.cpp` because SCons rejects building the same object in two
  environments; vLKp accepted that there and may clean it up after merging. In a new PR, look for
  a build arrangement that compiles the file once; if none works, include it and give the reason
  in the body, as #878 did.
- **Small over perfect.** vLKp would rather tidy up after merging than see a PR grow; keep
  adjacent cleanups for their own PR.
- **Transcripts on merit.** Offer a session transcript only when it would teach other users how to
  work with AI; the disclosure stands without one.
- **Approval is not the merge.** vLKp reviews in detail when next working on that code, so an
  approving comment can precede the merge by a while.
