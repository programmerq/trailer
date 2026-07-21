---
name: ev1-owner-decisions-2026-07-20
description: Four EV1 sourcing-side owner rulings (2026-07-20) — HV charger cannon PARKED-not-closed (with revival triggers), composite-flange fork deferred-long-term (capture pros/cons), SIR/airbag connector = OEM chosen + capture generic alternate, and windshield = keep all four routes open as strong standalone builder-choice proposals (a new research workstream).
metadata:
  type: project
---

Owner rulings relayed 2026-07-20, folded into the programmerq/ev1 sourcing catalog.

**1. HV cannon (charger 5-way HV connector, drawing PSMELC68239AA) — PARKED, NOT closed.** Verbatim: "Park for now, but not indefinitely. It'll take some digging on my end and we're in no hurry. That just doesn't mean forget it forever." Disposition = PARKED-BY-OWNER 2026-07-20 with revival triggers: (a) the charging build reaches the point of needing the connector, or (b) the owner surfaces a physical unit/drawing. One of the genuinely donor-less HV bespoke connectors (with the APM 7/5-way cannons + BPM 14-way 12160544).

**2. Composite-body flange fork (PR #25) — DELIBERATELY-DEFERRED-LONG-TERM.** Verbatim: "Capture the pros and cons in the document for now. We won't be making decisions on that for a very long time." The Fiero-style composite body has no native steel pinch-weld, so each aperture needs a flange choice — mold-in ~3mm rigid lip / bonded metal strip / adhesive-base seal on a flat ledge. Ruling: capture FULL pros/cons per option in `weatherstrip_seal_profiles`; decide per-aperture when panel molds approach. Not decided now.

**3. SIR/airbag connector fork (PR #20) — OEM chosen + capture generic.** Verbatim: "OEM, but capture the generic part number." Chosen route = OEM SIR connector 12126040 / PT1745 ($63-122, GM J-38125 "do not substitute" mandate). ALSO documented in-case: the ~$8 generic yellow 2-way as the alternate for inert/non-live-SIR bench builds. Decisions 1-3 landed as small case-file edits (rolling PR claude/owner-decisions-2026-07-20).

**4. Windshield four routes (PR #25) — KEEP ALL FOUR OPEN as strong builder-choice proposals (NEW research workstream).** Verbatim: "Let's keep all of these angles open. They should each be researched further and be much stronger proposals. If two people build an EV1 they could pick which one to adopt." → deepen ALL FOUR (coated repro / fine-wire heated / heated film / unheated+forced-air) into strong standalone proposals: per route = named vendors with real quotes where obtainable, cost bands, MOQ reality, AS1/FMVSS-205 compliance path, fidelity assessment, and a builder-facing "pick your route" comparison. Explicitly NOT urgent — quality over speed; queued AFTER the pigtail + TJB + hotfix work; skeptic pass before final (per [[owner-skeptic-pass-before-final]]).

Related: [[ev1-seals-and-heated-windshield]] [[ev1-harness-sourcing-result]] [[ev1-double-check-2026-07-20]] [[owner-max-progress-adversarial-gate]] [[owner-skeptic-pass-before-final]] [[ev1-replica-selective-fidelity]].
