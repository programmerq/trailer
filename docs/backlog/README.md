# Trailer — Backlog

> One open work item per file. This directory is the in-repo backlog:
> a flat set of Markdown files, each a single follow-up, todo, or deferred
> item. It replaces ad-hoc "open items" scattered across session notes and
> team memory with something a reviewer can grep, diff, and close in git.

The design goal is **conflict-reducing** tracking across branches: two agents
on two branches can each add an item without editing a shared list, because
every item lives in its own file whose identity is a date + slug. Merges
almost never fight, because there is no central list to fight over — but the
scheme is not collision-*proof*. See *File naming* below for the two real
failure modes and how to resolve them.

## Relationship to TODO.md

Both [`../../TODO.md`](../../TODO.md) and this directory are live, and
[`AGENTS.md`](../../AGENTS.md) points at both. They are not interchangeable:

- **`TODO.md` is the running log.** It holds dated session records — HITL
  passes, reference-user smoke sessions, and multi-perspective audits — in
  the same narrative, append-mostly shape, plus free-form deferred-work
  notes captured in the flow of development. It reads chronologically; an
  entry can be a paragraph of diagnosis, a cluster of related observations,
  or a struck-through "Done" line. It is where friction gets *written down*
  as it is discovered.
- **`docs/backlog/` is the tracked item set.** Each file is a single,
  individually-actionable item with a checkable **threshold** (the G1 gate)
  and its own lifecycle: it exists while open, and its deletion in git *is*
  its closure. It is where a discrete follow-up gets *tracked to done*.

**Which to use when:** capturing what a session surfaced, or a note that
only makes sense next to its neighbours in a dated pass → `TODO.md`. Promoting
one concrete, closeable follow-up out of that log — something a PR can pick up,
meet a threshold for, and close — → a file here. An item may start life as a
line in a `TODO.md` session entry and graduate to a backlog file once it is
ready to be owned and closed on its own.

## File naming — the item id

Each item is one file named:

```
YYYY-MM-DD-<slug>.md
```

- **The date is the item's CREATION date**, not a due date. It is the day
  the item was written down.
- **`<slug>`** is a short kebab-case handle for the item (`macos-launch-no-open-panel`).
- **The filename stem (`2026-07-12-macos-launch-no-open-panel`) is the stable
  item `id`.** Reference it in commits, PRs, and cross-links. It never
  changes once the file exists.

Because each item is its own file, two branches that never saw each other can
each add items and merge without touching a shared list. This removes the
*common* source of backlog merge conflicts, but two failure modes remain —
know them:

- **Same slug + same day on two branches → identical filename.** Both
  branches create `2026-07-12-<slug>.md`, and the merge is a git add/add
  conflict on that path. Resolve by **keeping both bodies** and **extending
  one slug** until the filenames differ (`…-<slug>-forms.md` vs
  `…-<slug>-viewer.md`). Do not renumber — there are no numbers.
- **Slightly-different slugs for the same work → silent duplicate.** Two
  people file the same follow-up under `…-theme-live-wire.md` and
  `…-theme-not-applied.md`; git merges both cleanly and nothing flags it.
  There is no automatic guard — **dedupe when noticed**: keep the better-
  written file, delete the other with a commit message pointing at the id
  that survives.

## What an item contains

YAML frontmatter, then a short body. The seeded item
[`2026-07-12-macos-launch-no-open-panel.md`](2026-07-12-macos-launch-no-open-panel.md)
is a good, fully-filled example to copy from; the skeleton is:

```markdown
---
id: 2026-07-12-macos-launch-no-open-panel
title: macOS launch should be dock + menu bar only, no open panel
priority: high
status: open
source: owner dogfood report 2026-07-12
created: 2026-07-12
---

## Threshold

<The checkable pass/fail gate this item must meet before it is Done, per
the G1 rule "declare a checkable threshold before work begins" (AGENTS.md).
A number, or a behaviour phrased as an observable pass/fail. If it is not
yet declared, write "TBD — declare before work begins" and declare it before
any implementation starts.>

## Context

<Why this item exists and what "doing it" means. Link DESIGN / PHILOSOPHY /
decision-record sections and any `file:line` that anchors the work. Cross-link
related item ids.>

## Provenance (optional)

<Only if the `source:` frontmatter field doesn't already capture where this
came from — the session, PR, review flag, or owner message, with a date.
Redundant when `source:` says it, so omit it in that case.>
```

Frontmatter fields:

| Field | Meaning |
|---|---|
| `id` | The filename stem. Same string as the file name minus `.md`. |
| `title` | One-line human title. |
| `priority` | A value from the scale below. **Do not invent a priority the source did not give** — use the `TBD`/`unranked` sentinels instead. |
| `status` | Always `open`. Closure is deletion, **never** an in-place edit to `closed` — see *How items close*. |
| `source` | Provenance: which session, PR, review flag, or owner message. |
| `created` | The creation date — matches the date in the filename. |

Body sections: **Threshold** (the G1 gate) and **Context**; **Provenance** is
optional and only used when `source:` didn't fully capture the origin.

### The `priority` value space

`P0`–`P4` is the primary scale (P0 = drop-everything, P4 = someday). Two
sentinels cover the case where the source gave no rank:

- **`unranked`** — sort last; the source explicitly declared no rank.
- **`TBD`** — needs triage; a rank is expected but hasn't been assigned yet.

One legacy word is in use: **`high`** (the macOS-launch item, the owner's own
word) maps to "urgent — triage to a P-level when the item is picked up." It is
kept verbatim because it is the source's language, not renamed here.

**Rule:** prefer a P-level. Use `TBD`/`unranked` as explicit sentinels when
the source gave no rank — never invent a rank the source did not give.

## How items close

Closure is a git deletion, not a status edit.

- The PR that implements an item **deletes the item's file** and
  **references the item `id`** in its commit / PR message
  (e.g. `Closes backlog 2026-07-12-macos-launch-no-open-panel`).
- **Closures ride the code PR** (`closures-ride-code-PRs`, owner 2026-07-15,
  "code or it doesn't happen"): the file deletion is a **rider commit on the
  same PR that ships the implementing code**, not a separate bookkeeping-only
  PR. A PR whose entire diff is backlog-file deletions or status shuffling is
  not worth opening — fold the deletion into the nearest code PR that closes
  the item, with the evidence citation in that PR's body.
- The deletion in git history **is** the closure record: `git log` over the
  file shows when and in which PR it closed, and `git show` recovers its full
  text. No `status: closed`, no churn edits, no separate done-list to keep in
  sync. **Never set `status: closed` in place** — that would leave the closed
  item cluttering the working tree, which is exactly what deletion avoids.

This keeps the working tree showing exactly the still-open backlog at all
times, and keeps merges clean — a closed item is an absent file, and two
branches closing different items never conflict.

### Finding, reopening, and won't-do closures

- **List closed items** (deletions) with:
  ```sh
  git log --diff-filter=D -- docs/backlog/
  ```
- **Recover a closed item's rationale** — its full text at the moment before
  deletion — with:
  ```sh
  git show <sha> -- <path>
  ```
  where `<sha>` is the deleting commit from the log above.
- **Won't-do / obsolete / duplicate closures also delete the file**, but need
  no implementing PR. The deleting commit message must **state the reason**
  (won't-do and why, superseded by which id, duplicate of which id), since
  there is no implementing diff to explain the closure.
- **Reopening = recreate the file.** Reuse the **same id** if you are resuming
  the same work (so cross-links still resolve), and note in the body that it
  **supersedes the prior deletion** (cite the deleting commit's sha). If the
  work is meaningfully different, file a fresh id instead.

## Conventions

- Keep items short. An item is a pointer to work, not the design doc for it —
  link `DESIGN.md` / `PHILOSOPHY.md` / a decision record for the reasoning.
- If an item grows a real design debate, it graduates to a decision record in
  [`../decision-records/`](../decision-records/) — the active ADR directory
  (numbered records with a `Status:` line and an arbiter, per the G6 gate in
  [`AGENTS.md`](../../AGENTS.md)). Leave the backlog item as a one-line pointer
  to the record until the record is accepted and the work lands. (Note: an
  older [`../decisions/`](../decisions/) directory also exists and still holds
  a few earlier records, such as the empty-state window model; new records go
  under `../decision-records/`. Merging the two is out of scope for the
  backlog.)
- Priorities are copied from the source, never invented here (see *The
  `priority` value space*).
