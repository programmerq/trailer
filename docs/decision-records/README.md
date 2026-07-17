# Decision records

Adjudicated design decisions live here, one per file. A decision record (DR)
captures a fork that was argued out and settled, so a later agent can see *why*
a default, threshold, or invariant is the way it is without re-litigating it.

## Naming

```
docs/decision-records/YYYY-MM-DD-<slug>.md
```

- **Date** is the day the record is **first drafted** — not a due date, not the
  accepted date. It never changes once the file exists.
- **`<slug>`** is a short kebab-case handle for the decision
  (`permissionless-screen-capture`, `never-worry-save-invariant`).
- The filename stem (`2026-07-16-permissionless-screen-capture`) is the record's
  stable **id**. Reference it in prose, commits, and PRs as
  **"DR 2026-07-16-permissionless-screen-capture"**.

Copy `TEMPLATE.md` to a new dated file and fill every section.

### Why date + slug instead of sequential numbers

Sequential numbers (`0014-…`) force a shared counter. Two agents on two branches
both reach for "the next free number," both pick `0014`, and the numbers collide
at merge — which is exactly what happened (four parallel branches claimed `0014`
on 2026-07-16, and this directory already carries a three-way `0006` collision
from the same failure mode). Date + slug needs no shared counter: each record's
identity is the day it was drafted plus its own slug, so branches almost never
fight. This mirrors the `docs/backlog/` scheme exactly — see
[docs/backlog/README.md](../backlog/README.md).

### Collisions

The scheme is not collision-*proof*, just collision-*rare*:

- **Same slug, same day, two branches → identical filename** (a git add/add
  conflict at merge). Resolve by keeping both bodies and **extending one slug**
  to disambiguate (`…-capture-macos.md` vs `…-capture-linux.md`). Do **not**
  renumber — there are no numbers.
- **Slightly different slugs for the same decision → silent duplicate.** Dedupe
  when noticed.

## Lifecycle

A record moves through three states, tracked in its `Status:` line:

- **proposed** — drafted, argument captured, not yet adjudicated.
- **accepted** — adjudicated; the decision is in force. Set the accepted date.
- **superseded** — replaced by a later record. Point forward with
  `superseded-by <YYYY-MM-DD-slug>`; the superseding record points back to it in
  its Context.

An accepted record is what a G6-gated change references (see `AGENTS.md`).

## Grandfathered numbered records

Records that predate this scheme keep their names. Every numbered record already
merged to `main` — `0001`–`0013` in this directory, the accepted `0014` from
[#69](https://github.com/programmerq/trailer/pull/69) once it merges, and the
legacy files under `docs/decisions/` — stays as-is. They are referenced across
merged PRs, code comments, and memory; renaming them is churn with no benefit.

Everything **not yet merged to `main`**, and every **future** record, uses
`YYYY-MM-DD-<slug>`. Grandfathering is the default; the owner can override any
individual case at merge review.
