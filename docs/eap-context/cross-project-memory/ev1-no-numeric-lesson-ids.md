---
name: ev1-no-numeric-lesson-ids
description: Owner convention (2026-07-18) — do NOT use numeric/sequential identifiers (L1, L2, … L8) for methodology lessons in the ev1 repo's sourcing/sibling_identification.md; identify lessons by DESCRIPTIVE NAMES. Sequential max+1 numbers collide across parallel branches (mirror + lamp PRs both grabbed "L8").
metadata:
  type: feedback
---

**Owner ruling (2026-07-18):** the L1/L2/…/L8 numeric lesson identifiers in `sourcing/sibling_identification.md` (programmerq/ev1) are ABOLISHED. Lessons are identified by **descriptive names**, not sequential numbers. Owner: "using a numbering scheme … invites collisions and conflicts … I don't think these need numeric identifiers at all. The identifiers are the names."

**Why:** sequential max+1 numbering is a merge-collision magnet across parallel branches — the mirror PR (#4) and the lamp PR (#5) each independently minted an "L8," producing exactly the conflict/churn the owner is calling out. This is the same lesson as the abolished BL-NNNN counter and the abolished 4-char backlog suffix — GM/electricsim convention favors collision-free identifiers. (See [[electricsim-full-backlog-ids-not-short-suffix]].)

**How to apply:** in sibling_identification.md, each methodology lesson gets a **descriptive bold-heading name** (e.g. "Service part-numbers can be a false trail for cross-make parts"; "For a bespoke-housing lamp, the sibling is the electrical guts") — NO "L<n>" prefix. Cross-references (in the doc and in `sourcing/parts/*.{yaml,md}` case files) cite the lesson by name or concept, never by number. The scaled-applications table is keyed by PART NAME (already collision-free). Applied 2026-07-18 across main + open PRs #5/#6/#7 (numbers stripped, lessons named).

Related: [[ev1-sibling-id-methodology]] [[electricsim-full-backlog-ids-not-short-suffix]] [[pr-draft-ready-merge-policy]].
