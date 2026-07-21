---
name: figure-repair-traceable-shape-rule
description: Owner rejected redux #50's figure/procedure reassembly (untraceable shape); standing disciplines for all manual-restructuring work.
metadata:
  type: feedback
  modified: 2026-07-21T00:23:43.980Z
---

Owner rejection of merged redux #50's figure/procedure reassembly work, 2026-07-21 ~00:16Z. His spot-check of sir-147/148 found figures below their paired steps, bare figures stacked two+ in a row, and figures stranded past the wrong step. Verbatim rulings: "Per the convention in the figure pairings, the figure is supposed to go above the text it is paired with." / "we still have two figures in a row, which is incorrect" / "I only reviewed this small section and it's not correct at all. This isn't simple for LLMs to do, so don't assume you got it right just like we can't assume that marker was perfect." / (chat) "I could tell what happened when marker did all this, but now you've imposed a different shape that I can't follow at all."

**Why:** the failing class was #50's "scrambled procedure reassembly" fixes (4 SIR + 3 brakes regions) which rewrote page regions WITHOUT the two disciplines the prop-90/91 exemplar had: per-figure scan verification with independent adversarial re-read, and figpair markup making every move self-describing. Result: an untraceable third shape — neither marker's output nor a documented convention.

**How to apply (standing, for ALL manual-restructuring work in redux):**
1. Never impose a shape the owner can't trace: any deviation from marker's output must be self-describing (figpair marker on every moved figure) and mapped in the PR body (baseline → move → scan cite, per region).
2. Figure placement convention: figure immediately ABOVE its paired step/text block; never two bare figures stacked consecutively.
3. Every reassembled region requires fresh high-zoom scan reads + an independent adversarial re-read BEFORE commit — "don't assume you got it right"; unverifiable regions stay at marker baseline with a note.
4. Fix-forward with scoped region-reverts (restore marker baseline first, then re-apply verified pairing) was the agreed remedy over whole-merge reverts.
5. The 272-page re-pairing wave (BL-2026-07-20-figure-step-de-pairing-wave-re-pair-column) is FROZEN until the owner approves the redo PR's shape.

Link: [[owner-decision-queue]], [[verify-against-original-scans]].
