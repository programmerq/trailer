---
name: bl-refs-must-be-full-slug-ci-only-audit
description: Any BL-YYYY-MM-DD backlog reference in electricsim code/docs must be the FULL dated slug id; a bare date-only (BL-2026-07-20) or ellipsized (BL-2026…) id FAILS scripts/audit_work_item_refs.py --check, which is CI-ONLY (local make test + vat lint do NOT run it). Run it before pushing any commit that cites a BL id.
metadata:
  type: feedback
---

# BL references must be the full dated slug — the citation audit is CI-only

In programmerq/electricsim, every backlog reference must be the COMPLETE dated id `BL-YYYY-MM-DD-<lowercase-slug>` (e.g. `BL-2026-07-20-redproof-metric-kind-liveness-blind-spot`). A reference written as:
- a bare date-only prefix `BL-2026-07-20` (no slug), or
- an ellipsized/truncated id `BL-2026…` / `BL-2026-07-17` cut short,

is flagged `[MALFORMED-DATED]` by `scripts/audit_work_item_refs.py --check` and FAILS the CI "Lint & syntax checks" job (exit 1).

**Why it keeps biting:** this audit is **CI-only** — `make test` and `vat lint` do NOT run it, so a bad BL ref passes every LOCAL gate green and only fails in CI after push. (Hit twice: PR #279's truncated `BL-2026-07-17` ref, and PR #352's two bare `BL-2026-07-20` refs — both green locally, red in CI.)

**How to apply:**
1. Never write a bare or ellipsized BL id in code comments, docstrings, PR bodies, backlog notes, or commit messages — always the full `BL-YYYY-MM-DD-<slug>`. If you don't know the slug, look it up in `docs/backlog/` (the filename IS the id) rather than abbreviating.
2. Before pushing ANY commit that cites a BL id, run `python3 scripts/audit_work_item_refs.py --check` locally (exit 0 = clean; `malformed-dated > 0` = a bad ref). Put this in worker briefs that touch backlog refs.
3. `N tracked-dropped` in the audit summary is a pre-existing known state, not a failure — only `dangling`/`malformed-dated`/`dup-def` > 0 fail.

Related: [[electricsim-ci-only-shellcheck-and-checks-api]].
