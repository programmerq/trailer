---
name: redux-page-marker-tooling
description: ev1-manual-redux print-page HTML-comment markers — the gen_page_markers.py tool, coverage, and the Electrical figref-counter gotcha.
metadata:
  type: project
---

ev1-manual-redux carries print-page provenance as greppable HTML comments `<!-- page: <prefix>-N -->` (one per print-page boundary; N is the scan-page vocabulary, e.g. `brakes-203` ↔ `originals/EV1 brakes manual/brakes-203.jpg`). Built in PR #43 (branch `claude/page-break-metadata`), superseding the abandoned PR #37 (which blind-interpolated markers into meaningless clumps and broke CI by injecting inside flowchart-IR blocks).

Tool: `scripts/gen_page_markers.py` — `--inject` / `--check` / `--check-all` / `--audit`. Wired into `harness/ci/lint.sh` so marker drift fails CI. Two-tier model: **anchored** markers placed at real evidence (embedded `![](_page_N_*.jpeg)` figure refs → `reconstructed from print <prefix>-N` chart headings → fuzzy OCR match of `originals/.../<prefix>-N.txt`) and content-verified (must beat N±1); **filled** markers for blank/collapsed pages, stacked consecutively at a gap boundary (assert page ORDER only, never content — coarse boundary hints, skew bounded by run length). No-skip rule: every page in each manual's span gets a marker (no gaps). Never fabricates a wrong-page assertion — a page that can't anchor without asserting a neighbor's content stays filled.

Covered (8 manuals): brakes, body, chassis, propulsion, SIR, HVAC, battery, Electrical. Out of scope: EV1 Bus (topical spec, not page-linear) and the parts catalog / EV1 Master Indexes (section-coded pages like "BDFR 7-3", owned by the parts-catalog workstream).

**Electrical gotcha (cost real investigation):** the markdown's embedded `_page_N` figure-ref numbers are a per-image EXTRACTION counter, NOT a page index — they map many-to-one onto scans and drift 0–4 pages, so the figref anchor tier is disabled for elec (`disable_figref` cfg flag). BUT the scan `.txt` files themselves are perfectly page-aligned (all 290+ footer-bearing elec pages have printed footer == scan-N). So elec is anchored purely via OCR-text matching + chart citations with an off-by-TWO guard (`offby2` cfg flag). Lesson: trust `originals/.../<prefix>-N.txt` as page ground-truth; distrust the markdown `_page_N` counter.
