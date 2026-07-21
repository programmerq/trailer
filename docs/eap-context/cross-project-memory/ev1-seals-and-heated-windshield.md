---
name: ev1-seals-and-heated-windshield
description: EV1 replica weatherstrip seal-profile + heated-windshield sourcing result (2026-07-20, PR #25) — bespoke seals map to commodity extruded profile families with a per-aperture flange co-design table; heated windshield's invisible coating (not the shape) is the unobtanium wall, AS1/DOT-gated, with a ranked fallback menu. Two design forks posted as PR comments.
metadata:
  type: project
---

Owner blind-spots #5 (weatherstripping) + #9 (windshield) executed as research → one rolling PR. Both on branch `claude/sourcing-body-seals-glass`, **PR #20... actually PR #25** (ready, not merged).

**PR #25 — `sourcing/parts/weatherstrip_seal_profiles.{yaml,md}` (seal co-design):**
- All ~16 EV1 body seals are 27-block BESPOKE with no donor P/N, BUT each sits in a commodity extruded PROFILE FAMILY — so the replica's pinch-weld/flange geometry gets CO-DESIGNED to accept an off-the-shelf profile (owner's key insight; flows to frame/panel design).
- Profile families: F1 pinch-weld edge-grip bulb (door aperture, decklid — the workhorse, verified grip window 0.039–0.23″), F2 sponge bulb/D/P (lower/rocker/cowl), F3 glass-run channel (door glass), F4 locking gasket, F5 reveal/garnish molding (windshield/backlite perimeter), F6 hood-to-cowl bulb. Vendors: Trim-Lok, M M Seals, Steele, Metro, Precision — all buyable as cited.
- **Flange co-design table (the payoff):** door aperture → F1, mold a ~2.5–3 mm rigid flange, 6–12 mm bulb gap; decklid → F1 trunk bulb; door glass run → F3 groove ~4–5 mm glass; windshield/backlite → BOND-LEDGE geometry NOT a bulb (glass is structural urethane-bonded; reveal parts are trim over the bead). All EV1 location→profile mappings held at CANDIDATE (no EV1 seal cross-section measured yet).
- R4 correction made during authoring: hood-to-cowl seal is **27002483** (redux 08-12 #013), NOT 27003040 (which is #022 RAIL-RF SI, a roof side rail) — the research draft had it wrong.

**PR #25 — `sourcing/parts/heated_windshield.{yaml,md}` (deepens the glass case):**
- The existing glass case (`sourcing/parts/glass.{md,yaml}` on branch `claude/sourcing-sweep-2`, NOT yet on main) treats the windshield as shape-only. This case adds the heated-layer crux. EV1 windshield = **27000159** (redux 08-12 #005); "heated" is a spec attribute, not in the redux row.
- The wall is the invisible whole-area heating COATING, not the shape. Lineage: Ford **Instaclear/Quickclear** transparent conductive metal-oxide coating + bus bars, drew ~36V unrectified alternator AC (12V can't push whole-screen current — same reason modern EVs use battery-DC/48V); resurgent EV tech, capability NOT lost. ("Electriclear" GM trade name = weakly attributed, downgraded.)
- **Ranked fallback menu, all under the FMVSS-205 / ANSI Z26.1 AS1 road-legal gate (laminated, ≥70% VLT vision area, AS1-marked, DOT-registered self-cert):** coated repro (max fidelity, ~25-unit MOQ) → **Tyneside Safety Glass fine-wire heated** (realistic low-volume route, lower visual fidelity) → heated film (cheap/invisible but AS1-VLT legality weak point) → unheated + forced-air (cheapest, loses the no-vents-efficiency story). Recommended realistic path: Tyneside fine-wire unless a coated-repro live quote is acceptable.
- **Oral history (`@oral-history`, V212/Questionable Garage via v212.org, corroborated by The Autopian):** the wooden-jig + real-glass-company plan; minimum-unit recorded as an honest discrepancy — **The Autopian says 25 windshields**, owner recalled ~100. V212 ultimately used a GM-donated original from donor VIN 159 (non-repeatable path).
- EV1 exact construction kept MEDIUM confidence: "coating, possibly with wiring" — sources conflict (invisible embedded wiring per Car and Driver/EV1 engineer vs gold-foil coating per V212 coverage); not asserted either way.

**Two design forks posted as non-blocking PR #25 comments:**
1. Composite-body flange decision — the Fiero-style composite body has no native steel pinch-weld, so each aperture needs a deliberate choice (mold-in ~3 mm rigid flange lip / bonded metal flange strip / adhesive-base seal on a flat ledge), decided BEFORE aperture molds are cut. Frame/panel design input.
2. Windshield fidelity-vs-cost-vs-legality — coated repro vs fine-wire vs unheated+forced-air, all under the AS1/DOT gate; film flagged as the legality weak point.

Related: [[ev1-catalog-sweep-complete]] [[ev1-harness-sourcing-result]] [[ev1-replica-lamps-and-panels-plan]] [[ev1-replica-sourcing-plan]] [[owner-max-progress-adversarial-gate]] [[ev1-sourcing-pr-granularity]].
