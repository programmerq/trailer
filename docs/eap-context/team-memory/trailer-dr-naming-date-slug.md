---
name: trailer-dr-naming-date-slug
description: Owner ruling 2026-07-16, now MERGED via PR #75 — decision records use date+slug filenames (docs/decision-records/YYYY-MM-DD-<slug>.md, same as backlog) to avoid parallel-branch numbering collisions; accepted 0001-0013, legacy docs/decisions/, and PR #69's 0014 grandfathered
metadata:
  type: project
---

On 2026-07-16 parallel sessions collided on "ADR 0014" — already claimed by unmerged PR #69's *accepted* recorder ADR (`0014-ux-recorder-screen-recording-permission-reconciliation.md`), then by the capture-preflight fix on `fix/capture-permission-flow`, and by PR #73. Sequential numeric prefixes collide when multiple branches author records in parallel; date+slug does not. Owner ruled: "let's use a date and slug scheme to avoid collisions across parallel branches for ADR numbering."

This convention is now **MERGED to main** via PR #75 (https://github.com/programmerq/trailer/pull/75) from branch `docs/dr-naming-convention` — it is no longer proposed/owner-gated/unmerged. Codified in `docs/decision-records/README.md`, with `TEMPLATE.md`, `PHILOSOPHY.md`, and `.claude/skills/review-before-push/SKILL.md` all updated to drop the "next free number" instruction.

**How to apply:**
- Name new decision records `docs/decision-records/YYYY-MM-DD-<slug>.md` — date = day drafted, slug unique in the directory — mirroring `docs/backlog/`. Never assign sequential numbers to new records.
- Drop the numeric prefix from the `# ` title heading too; identity lives in the dated filename + the `Date proposed:` line. Rekey any record-local threshold assertions to a slug-derived prefix instead of `G<n>.x` (e.g. `GPSC.x` = permissionless-screen-capture, since there is no number to key `G15.x` off).
- The persona/arbiter machinery is **unchanged** — four unranked lenses + admissible-objection test + arbiter verdict per `docs/decision-records/TEMPLATE.md`. Only the filename/numbering changed.
- **Grandfathered (keep numbers, cite by number):** accepted numbered ADRs **0001–0013** on main (0001-select-all-semantics.md is merged and cited as "ADR 0001" in `docs/platform-conventions.md`), the legacy `docs/decisions/` numbered files, and PR #69's accepted **0014** — blessed by name as the canonical 0014 when it merges. Do NOT renumber these.
- Pre-existing harmless anomaly: `0006` is used by three files on main. Do NOT "fix" it.

First record authored under the new scheme: `docs/decision-records/2026-07-16-permissionless-screen-capture.md` (see [[trailer-permissionless-capture-adr-pr72]]).

Related: [[trailer-requirements-summary]], [[proceed-on-clear-defaults]], [[trailer-review-before-push-policy]].
