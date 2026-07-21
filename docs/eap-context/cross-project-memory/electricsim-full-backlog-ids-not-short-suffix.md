---
name: electricsim-full-backlog-ids-not-short-suffix
description: REVERSED 2026-07-18 — owner abolished the random 4-char suffix on backlog ids entirely; canonical id is now bare BL-YYYY-MM-DD-<slug> (still with a plain-language gloss on first use). Supersedes the prior "cite the FULL suffixed id" rule. Agents scrub every suffix on their own branches NOW, in parallel with the main migration.
metadata:
  type: feedback
  modified: 2026-07-18T05:04:20.428Z
---

## CURRENT RULE (owner ruling 2026-07-18 — REVERSES the prior guidance below)

**Owner ruling, verbatim (relayed via the coordinator session as an owner quote, 2026-07-18):**
> "Ask every agent to scrub every suffix. If that means it'll be noisy? fine."

The random 4-char suffix on backlog ids (`BL-YYYY-MM-DD-<slug>-<rand>`, e.g. the trailing
`45qv`, `jwaz`, `wmyn`, `zehi`) is **ABOLISHED**. The canonical id form going forward is
**bare date+slug: `BL-YYYY-MM-DD-<slug>`** — with no random suffix in any context.

The one part of the old rule that STANDS: still cite an item **with a plain-language gloss on
first use** (AGENTS.md §9 + CLAUDE.md's no-bare-shorthand rule). So the correct citation is
`BL-YYYY-MM-DD-<slug>` + gloss — never a bare id, and never the old suffixed form.

### An atomic migration is running on `main` in parallel
A single atomic migration on `main` renames all `docs/backlog/*.md` files to bare date+slug and
sweeps bare-suffix strays in code comments (e.g. one at `ev1/ad/ad_host.cpp:404`).

### Do NOT wait for the migration — scrub your OWN branch NOW
On every open branch you touch, scrub every random suffix immediately (owner accepts the noise):
- **Rename** the `docs/backlog/*.md` files your branch adds/modifies to bare date+slug.
- **Rewrite** every suffixed reference AND every bare-suffix reference (e.g. a lone `45qv`) in the
  files you touch to `BL-YYYY-MM-DD-<slug>` + gloss. **Commit messages are exempt** — git history
  stays as-is; do not rewrite already-committed history.
- **Run the doc audits**: `audit_work_item_refs --check` and `audit_note_xrefs`.
- **Push** and drop a **PR comment** noting the scrub.
- When the main migration lands, resolve any mechanical conflict via **merge-of-main + regenerate**
  (the generated scorecards/backlog files), never hand-edit.

### Reconciliation with the prior rule (so this file isn't self-contradictory)
The OLD concern was correct in spirit: never cite the bare 4-char suffix ALONE (e.g. `45qv`), because
it's not a stable, searchable, or human-meaningful identifier. But the **resolution has changed**: the
old fix was "expand to the FULL suffixed id `BL-YYYY-MM-DD-<slug>-<suffix>`." That form no longer
exists. The new fix is the **bare DATE+SLUG id `BL-YYYY-MM-DD-<slug>`** — the random suffix is gone
entirely. Whenever the history below says "use the full suffixed id," read it as "use the bare
date+slug id."

See [[no-bare-backlog-suffixes]] for the companion note.

---

## HISTORY (SUPERSEDED — kept for context; the mitigation grep still applies, target form changed)

Owner had flagged, on multiple PRs within a single session (2026-07-17/18, PRs #289 and #287), that
backlog references in committed text must use the FULL dated id `BL-YYYY-MM-DD-<slug>-<suffix>`, never
the bare 4-char random suffix alone (`jwaz`, `wmyn`, `zehi`, …). Agents systematically regressed on
this and each miss cost a fix round-trip. **As of the 2026-07-18 ruling above, the suffixed form is
itself retired — the target is now the bare date+slug id.**

The no-bare-shorthand rule still exists — AGENTS.md §9 ("never cite an ID bare — always with a gloss")
plus CLAUDE.md's no-bare-shorthand rule — but agents kept forgetting it in practice.

**Why (still valid):** a bare short random suffix is not a stable, searchable, or human-meaningful
identifier on its own. The owner wants a stable id AND a plain-language gloss. The 2026-07-18 fix takes
this further: eliminate the random suffix from the id format altogether.

**How to apply (the grep mitigation, retargeted to the new form):**
- Before committing/pushing ANY change that references or closes backlog items, GREP the diff's added
  lines for bare short-form suffixes — e.g. `\b[a-z0-9]{4}\b` near backlog context, or the specific
  suffixes you introduced — and expand every one to its bare `BL-YYYY-MM-DD-<slug>` id with a gloss.
- `audit_work_item_refs --check`/`--labels` does NOT catch a bare suffix that isn't formatted as a
  work-item ref, so this needs an explicit manual/grep step, not just the audit gate.
- Put this instruction directly into any agent prompt that touches backlog-referencing code or docs.

Related: [[owner-prefers-larger-prs]], [[pr-draft-ready-merge-policy]].
