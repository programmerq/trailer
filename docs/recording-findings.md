# Recording Findings

The protocol for the owner's webcam+screen self-recorder: how a passive
capture of **real** Trailer use becomes structured findings that enter
the same queue as everything else. Like its sibling
[`smoke-session.md`](smoke-session.md), this document is deliberately
small — if it grows past a protocol into a spec, it has failed.

## What this is, and isn't

`smoke-session.md` already draws two of the three lines this protocol
needs, and this is the third:

- **Not a HITL pass.** *"A HITL pass is the maintainer driving their own
  daily workflow on a real build and writing down everything that
  annoyed them."* HITL passes are active and curated — the maintainer
  stops, notices, and writes.
- **Not a smoke session.** *"A short, repeatable observation session in
  which a person who is not the maintainer opens a fresh Trailer build
  for the first time and performs three small tasks"* — scripted, one
  sitting, ~20 minutes, a note-taker in the room.
- **This is passive self-capture during real use.** No script, no
  observer, no note-taker, no in-the-moment writing. The owner just does
  the document work they'd be doing anyway — J1–J8 or whatever else came
  up — with the recorder running in the background. Friction that's too
  small, too habitual, or too easy to rationalize away in the moment to
  ever make it into a HITL note still shows up on the tape. The agent
  that later mines the footage plays the role `smoke-session.md` gives
  the note-taker, after the fact and from video instead of in the room.

It is also **not telemetry**. The recorder is the owner's own tool, run
at the owner's own discretion; the capture is local, lives only on the
owner's disk, and nothing about it phones out — consistent with
[`../PHILOSOPHY.md`](../PHILOSOPHY.md)'s local-first, no-telemetry
constraint, which this protocol does not touch.

## Capture discipline

- **Start.** Whenever the owner is about to do real document work on
  Trailer — not a staged demo, not a re-run of a known bug. If it isn't
  a session the owner would have had anyway, the footage isn't the kind
  this protocol is for.
- **Stop.** When the session ends, or at the owner's discretion at any
  point (screen contents unrelated to the work at hand, an interruption,
  anything the owner would rather not have on tape). Stopping loses
  nothing upstream — an unrecorded session simply produces no findings.
- **What to record.** Both streams together: the **screen** (what
  Trailer did) and the **webcam** (the owner's reactions — a pause, a
  frown, a "why won't this—") — the same signal a smoke session's
  note-taker would be watching for, just unattended.
- **Storage location.** *Owner-config placeholder.* Where the recordings
  land on disk is the owner's call, set locally, and out of scope for
  this document — it only needs to be somewhere the mining agent (below)
  can read.

## The agent-mining contract

The durable artifact is the **finding**, never the raw video — the video
is discarded or kept at the owner's discretion once it's been mined. An
AI agent watches a recording and, for each moment of friction it
observes, emits a self-contained finding with exactly these fields,
matching [`../CRITERIA.md`](../CRITERIA.md) §6's shape:

- **Timestamp** — where in the recording it happened, so a human can
  scrub straight to it without rewatching the whole session.
- **Repro** — enough to reproduce on demand: the document or document
  type in use and the action taken, written as what the owner *did*, not
  what the agent *thinks*. Same rule `smoke-session.md` states for its
  note-taker: *"Behaviours beat opinions. 'Clicked Tools menu, scanned it
  for ten seconds, asked where the highlighter was' is data. 'I think the
  highlighter is hard to find' is interpretation."* A mining agent that
  writes findings as opinions ("the toolbar is confusing") has failed the
  contract as surely as a note-taker would. Mined from a single viewing
  rather than a deliberate HITL repro, so a hint is the honest bar — a
  follow-up session may be needed to firm it into a full repro before
  it's actionable.
- **Job** — one of **J1–J8** from [`../CRITERIA.md`](../CRITERIA.md) §5,
  or none if the moment doesn't map to a listed job.
- **Degree** — **Blocks** / **Workaround** / **Mars**, the same ladder
  CRITERIA.md §4 defines: the job couldn't be finished, finished only
  with extra steps, or finished but marred.
- **Platform** — **Windows** / **Linux** / **macOS**, the OS the session
  ran on. A discrete field, not a note folded into the repro: CRITERIA.md
  §1 scores parity per-OS and the §4 sort consumes it, so a finding that
  drops it can't be placed.

## Triage: no separate backlog

A recording finding is not a special case. It is computed and slotted
exactly like every other channel CRITERIA.md §6 lists: priority is read
off (job, degree, frequency) by the §4 ranking function, and the finding
lands in [`../TODO.md`](../TODO.md)'s ranked queue — or *Someday* if it
names no job, or the *Owner questions* ledger if the job or degree call
itself is the ambiguity. There is no recorder-specific queue, backlog,
or file to check separately; a finding that isn't in TODO.md's queue
hasn't been mined yet, not filed somewhere else.

## Existing recordings

The 2–3 recordings that already exist predate this protocol and are
**already reviewed and closed — out of scope.** Nothing in them needs
re-mining. This document governs recordings made from here forward.

## Why this exists

HITL passes catch what the maintainer notices and stops to write down.
Smoke sessions catch what a stranger notices on a fixed script. Neither
catches the friction that's ordinary enough to not interrupt real work —
the workaround the owner has already half-learned to route around
without registering it as a bug. A passive recording, mined later by an
agent with no stake in defending yesterday's design decisions, is how
that last category gets seen at all.
