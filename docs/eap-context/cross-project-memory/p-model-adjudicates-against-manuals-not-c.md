---
name: p-model-adjudicates-against-manuals-not-c
description: Owner standing norm (2026-07-19) — the P formal model adjudicates the C against the AUTHORITATIVE manuals; never re-model P to match the C. "The C is newer" is never evidence of correctness. Every C-vs-model divergence gets a 3-verdict manual adjudication.
metadata:
  type: feedback
---

Owner methodological challenge on electricsim#337 (verbatim): "For 337, it seems like the P model should have been able to catch the incorrect implementation in C in the first place. I think the P session may be treating the C code as settled truth?" — and (verbatim): "the C is newer is never evidence of correctness." Extended to #338: "that goes for 338 too."

**The norm (direction of truth).** The P model encodes what the EV1 SHOULD do per the authoritative sources — redux DTC catalog, brake/battery manual behavior + FAILURE-ACTION tables, patent choreography. **The C is the artifact UNDER TEST.** The P model's job is adversarial spec-checking of the C. NEVER re-model P to match observed/merged C behavior and call the old model a "bug" — that assumes C correctness and destroys the model's ability to catch the C defect.

**Three-verdict adjudication — apply to EVERY C-vs-model divergence, cite the manual page:**
- **(a) manual says the C is right** → the model change stands; cite the manual page.
- **(b) manual says the OLD model was right** → the C is the bug: file it, REVERT the model change so `p check` catches the C defect, and route the C fix. This is the whole point of the model.
- **(c) manual silent/ambiguous** → mark `@inferred` with the reasoning; NEVER silently pick the C.

**Trigger smells (any of these = you're probably C-favoring):** "re-model to match the merged C"; reclassifying a model reaction as a "latent model bug" because the C does less; "refreshing" model-side descriptive facts/thresholds toward current C (e.g. an isolation threshold 100→600 kΩ, a timing constant) without a manual cite; accepting a C behavior change as the new reference during a provenance cite-resync; trusting an in-C comment ("corrected from prior over-reaction", "provisional pending FAILURE-ACTION read") as authoritative.

**How it went wrong (the #337 case).** The #300 C reclassified motor open-in-run DTCs 064/065/153/154 to information-only; #337 re-modeled P to match and called the model's ABS/EMB-disable a "latent safety bug" — a C-favoring adjudication with NO manual check. The corrective follow-up must adjudicate each divergence (incl. the 071/074/152/147/148 reaction policies, the BPM thermal-268 finding, the resync's AD isolation 600 kΩ refresh + concept-preserved re-points) against the manuals, per a/b/c, page-cited. Manual-answerable → source+implement (this is [[manual-answerable-findings-source-and-implement]] applied to model-vs-C direction).

Related: [[manual-answerable-findings-source-and-implement]], [[p-formal-methods-workstream]].
