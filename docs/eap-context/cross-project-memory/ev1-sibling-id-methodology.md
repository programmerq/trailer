---
name: ev1-sibling-id-methodology
description: The piloted agent methodology for EV1 sibling/direct-lift part identification, its two benchmark results (both now confirmed), and the key lessons — GM re-boxes cross-make/variant parts under its own service numbers, so the discriminator lives on the physical part.
metadata:
  type: project
---

Piloted 2026-07-18 (owner-directed). A repeatable agent method to decide, per authoritative EV1 part, whether it's a **direct-lift** (same P/N), a **sibling-hint** (donor on another vehicle), or **bespoke** — precision over recall, defeating the "yup, picture of a hinge" pattern-match.

**Lives in the umbrella repo** [[ev1-umbrella-repo]]: methodology at `sourcing/sibling_identification.md`; per-part benchmark cases at `sourcing/parts/<id>.{yaml,md}`. Shipped as **PR programmerq/ev1#2** (branch `claude/sibling-id-pilot`) — ready-for-review as of 2026-07-18 (rack commit 8e172a3, switch commit 633f6f8). Merge is the owner's call.

**The 4-stage method:** (1) discriminating **feature spec** — MUST-MATCH vs TRIM-ALLOWED, each feature attributed to the *exact subassembly*; (2) multi-angle **blind** candidate generation (P/N/connector interchange + platform reasoning + cross-make P/N-series), blind to any known answer, flag ambiguity not assert; (3) **adversarial refutation** — default-reject refuter per candidate, must name the one physical check that settles it; (4) converge + score on a confidence taxonomy (architecturally-consistent < hard-fingerprint-confirmed < physically-confirmed/`@source:owner-in-hand`).

**Benchmark scorecard (both now confirmed against owner ground truth):**
- **Switch (turn/wiper stalk, EV1 P/N 27002430 lever + 27000758 wiper) = HIT, hard-confirmed.** Two blind angles both to 1996 Chevrolet Cavalier / Pontiac Sunfire (GM J-body); correctly rejected the Saturn-branding trap. Refuter first held it at AMBIGUOUS (public evidence couldn't confirm to housing level). RESOLVED 2026-07-18: a molded **GM<->ITT family ID plate** on a J-body wiper switch lists 27000758 (ITT 250.033/PULSE) on the SAME injection-molded housing as 22581546 / 22648264 etc. — proving it's an off-the-shelf ITT J/N-body switch, a **service renumber**, NOT bespoke. Debunked an uncited Gemini claim that 27000758 was "bespoke tool-molded for the EV1." One open check: which ITT family end-cap style matches the owner's in-hand switch.
- **Rack (EV1 P/N 26032740) = MISS by blind method, owner-confirmed Saab.** Owner's answer = powered rack from a 1985-1998 Saab 9000. Blind method never reached Saab (round 1 anchored on "electrohydraulic" to Toyota MR2; round 2 found the Saginaw 260xxxxx family and even argued AGAINST Saab). RESOLVED 2026-07-18 by owner's photo of the V212 rack casting: a cast **SAAB logo + Saab number** ("SAAB 40-04842") alongside GM casting "28004155 L-2" — the rack is physically Saab; GM service number 26032740 is a re-box.

**KEY LESSONS (all confirmed in the pilot):**
- **L2 — the manual's GM service P/N is a FALSE TRAIL for cross-make / variant parts.** GM re-boxes a sourced-or-variant part under its OWN service number (rack: Saab -> 26032740; switch: ITT J/N-body part -> 27000758). The real discriminator — a **casting mark or molded maker's ID plate on the physical part** — is NOT in redux. Cross-make sourcing needs a **physical evidence capture** input (owner photos) the manual can't supply.
- **L3 — attribute each discriminating feature to the RIGHT subassembly** (the rack's "electrohydraulic" character is the PUMP, not the rack).
- **L5 — part-number PREFIX blocks are NOT semantic.** GM 8-digit numbers are assigned sequentially; the EV1's 27-block is just the program's service-SKU allocation (Saturn Service Parts Operation), not a "variant/restricted-run" code. Beware AI tools (Gemini) inventing decode rules without sources — verify against primary evidence.

**Tooling reality:** NO reverse-image-search API. **Hugging Face image-similarity is a dead end here** — the only compute tool (`dynamic_space`) has `invoke` globally disabled (`gradio=none`), no CLIP/embedding task, no HF_TOKEN. Method relies on Claude-vision (high-zoom scans + listing photos) + text interchange research. Decisive signals were text/photo-anchored (P/N-series, connector/ITT plates, castings), not automated visual similarity.

**Companion fact (owner, 2026-07-18):** GM parts-binned from OTHER makers too — the EV1 external keypad is a Ford part fitted with an EV1-specific bezel, so it's NOT a direct P/N drop-in. Canonical "donor part, bespoke bezel/adaptation" pattern (methodology L2b) — maps to the MUST-MATCH (donor core) vs TRIM-ALLOWED (bezel) split.

**Still-open owner question (Q3, additive):** what confidence tier justifies buying a junkyard part to bench-check. Owner to set a price cap.

Related: [[ev1-replica-sourcing-plan]] [[ev1-replica-part-candidates]] [[ev1-umbrella-repo]] [[manual-answerable-findings-source-and-implement]].
