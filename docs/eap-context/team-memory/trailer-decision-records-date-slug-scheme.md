---
name: trailer-decision-records-date-slug-scheme
description: Owner ruling 2026-07-16 — decision records switch from numeric prefixes (0001..) to a date+slug scheme docs/decision-records/YYYY-MM-DD-<slug>.md to avoid parallel-branch number collisions; 0002-0013 and PR #69's 0014 are grandfathered
metadata:
  type: project
---

On 2026-07-16 the owner ruled that decision records move from numeric prefixes to a **date+slug** filename scheme: `docs/decision-records/YYYY-MM-DD-<slug>.md` (same convention as `docs/backlog/`). Trigger: a same-day "0014 pileup" — two open branches independently claimed ADR **0014** (PR #69 `feat/ux-recorder` added an *accepted* `0014-ux-recorder-screen-recording-permission-reconciliation.md`, and `fix/capture-permission-flow` also took 0014 for the corrected preflight-gated capture flow). Sequential numeric prefixes collide when multiple branches author records in parallel; date+slug does not.

**How to apply going forward:**
- New decision records: name them `YYYY-MM-DD-<slug>.md`. Drop the numeric prefix from the `# ` title heading (the dated filename + `Date proposed:` line carry identity). Rekey any record-local threshold assertions to a slug-derived prefix instead of `G<n>.x` (e.g. this record used `GPSC.x` = permissionless-screen-capture, since there is no number to key `G15.x` off).
- **Grandfathered (keep their numbers, reference by number):** accepted records already on `main` **0002–0013**, and PR #69's **0014**. Do NOT renumber them; cross-reference them as `0013`, `0014`, etc.
- The persona/arbiter machinery (four unranked lenses + admissible-objection test + arbiter verdict, per `docs/decision-records/TEMPLATE.md`) is unchanged — only the filename/numbering changed.
- Pre-existing anomaly still on main: `0006` is used by three files; harmless, do not "fix".

First record authored under the new scheme: `docs/decision-records/2026-07-16-permissionless-screen-capture.md` (see [[trailer-permissionless-capture-adr-pr72]]). Related: [[trailer-review-before-push-policy]], [[trailer-requirements-summary]].
