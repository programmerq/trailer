---
name: schematic-readability-cant-baseline-denser-sheets
description: electricsim's audit_schematic_readability is CI-ADVISORY (never gates CI) and is already globally red on main. When Gate-II realization work legitimately makes a module's sheet denser (real silicon added), a resulting readability regression often CANNOT be recorded by the tool — document it in the PR + track a layout BL instead of forcing a baseline write.
metadata:
  type: project
  modified: 2026-07-21T02:22:00.572Z
---

# schematic-readability regressions from added silicon: document + track, don't force a re-baseline

Discovered 2026-07-21 finalizing Gate-II R5 PR-A (#366), which promoted BTCM's 7 low-side telltale/PWM stand-ins to real N-MOSFET driver triads + added a UART transceiver — a legitimately denser sheet (crossings 84->182, aspect 1.389->1.144, out of the landscape band [1.2,1.8]).

**Key facts about `scripts/audit_schematic_readability.py` + `export/kicad/readability_baseline.yaml`:**
- Tier is **ci-advisory** — it NEVER fails a PR / CI. It is also **already globally red on `origin/main`** (most modules fail; e.g. ad aspect 1.848, sdm 1.095 fail the hard aspect floor). So a new red here does not block merge and does not mean the branch broke a green audit.
- You often **cannot record a legit regression via the tool:**
  - `--allow-regression` waives only ratchet axes (crossings/bends/fill/followable), **NOT hard floors**. The landscape aspect band is a non-waivable hard floor (`_floor_findings`, elk rubric 2). No aspect grandfather exists (there is a `FILL_FLOOR_GRANDFATHER`, but not for aspect).
  - `--update` has **no per-module scope** — it rewrites all rows or none; there is no "re-baseline only rsa/btcm" flag.
  - A global `--update` is **unconditionally refused** while any module (e.g. pre-existing ad/sdm) fails a hard floor — so you can't re-baseline your module without first fixing unrelated modules (out of scope).
  - Hand-editing the baseline is forbidden (file header + AGENTS.md merge-discipline).
- A rubric 2->1 downgrade (`--allow-rubric-downgrade`) is an owner-flavored floor-semantics change — do not do it unprompted.

**Disposition (the reusable pattern):** when realization work makes a sheet denser and readability regresses on this advisory audit and the tool can't record it — leave the baseline BYTE-IDENTICAL to main, state the regression + why + that it's non-recordable transparently in the PR body, and file a layout-improvement BL (e.g. `BL-2026-07-21-btcm-schematic-sheet-layout-improvement-gate-ii`). Do NOT force it, do NOT hide it behind the advisory tier silently. This will recur across the Gate-II track (each module gaining real silicon). Note if a module IMPROVES (like RSA did here), the tool still fires "must re-baseline" but the same no-per-module-scope limit applies.

Committed PNGs come from `scripts/kicad_sch_preview.py --dpi 300` (matplotlib host previewer), NOT the Docker kicad-cli path (PDF-only); a module export change must regenerate that module's PNG (the png-render-freshness gap). Relates to [[manual-answerable-findings-source-and-implement]].
