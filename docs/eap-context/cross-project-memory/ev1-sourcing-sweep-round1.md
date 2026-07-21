---
name: ev1-sourcing-sweep-round1
description: EV1 sourcing sweep round 1 (PR #7) — confirmed direct-lifts (stoplamp switch, ABS sensors, Saturn-coupe door handle), two closed catalog unknowns (brake pressure sensors, steering-column J-car lineage), and the remaining sweep worklist.
metadata:
  type: project
---

Cluster 3 (rolling expected-value sweep) of the owner's big fan-out (2026-07-18). Round 1 = PR programmerq/ev1#7 (branch claude/sourcing-sweep-1), ready-for-review; 6 case files under sourcing/parts/ + closed 2 catalog.md open rows. Worklist for future rounds: scratchpad/sweep_worklist.md (17 clusters / 10 PR-groups).

**CONFIRMED direct-lifts (buy outright, HIGH):**
- Brake-light/stoplamp switch 15981543 → ACDelco D1521E (supersedes 15981543→15741137→15128745); donors 1994-2005 GM S-body (Astro/Safari/S10/Blazer/Jimmy/Sonoma/Bravada). 6-blade brake-pedal-bracket snap-in.
- Front ABS wheel-speed sensors 10456045 (RH) / 10456046 (LH) → Delco ABS-VI compact-car front WSS; ACDelco 19259629/19259628; donors Cavalier/Sunfire/Skylark/Grand Am/Achieva/Beretta/Corsica/DeVille; aftermarket SMP ALS204/207, Dorman 970-001/002. Cross-confirms EV1 ABS-VI.
- Front inside door handle 21093972/21093973 → Saturn SC1/SC2 COUPE (1993-94) — ANCHORS the EV1 front door as a Saturn S-series coupe door (parts-bin lineage consistent with Saturn Service Parts Operation).

**Two catalog OPEN unknowns CLOSED:**
- Brake pressure sensors 18022928 (downstream) / 18024980 (upstream): standard Delco ABS-VI has NO pressure sensors — these are EV1 REGEN-braking additions with NO donor vehicle → spec-match only, LOW donor-confidence. 3-wire ratiometric 0-5V transducers on Delphi Metri-Pack 150 3-way (12110192: A=gnd/B=+5V/C=signal); o-ring boss seal (special o-rings 18023082 up / 18023081 down) — FIT RISK: the owner's modified pedal-feel-emulator M/C ports must present the matching o-ring boss (mechanical fit is the real risk; electrically trivial). BPMV solenoid kit 18023464 → HIGH family cross (ACDelco 18020566, ABS-VI pool Cavalier/Sunfire/Grand Am/Beretta ~1992-99); caveat EV1 4-sensor/dual-master regen vs mainstream 3-channel — confirm channel count. EV1 shares cover kit 18020567 outright.
- Steering column: SIR clockspring 26036217 = CONFIRMED Chevrolet Cavalier / Pontiac Sunfire 1995-99 J-body (supersedes 26036217→26065909→26087272), HIGH — grounds the owner's Cavalier-tilt-column plan with a real number. Column casting 26058359 = candidate only (no public cross; 26xx Saginaw block like the re-boxed rack 26032740 → maybe re-boxed J-family, LOW-MED); TILT vs fixed UNRESOLVED. Cavalier tilt column = strong trial-fit candidate (verify length, intermediate-shaft coupling to rack 26032740, dash bracketry, ignition-lock housing).

**Other:** wiper MOTOR 27002935 = EV1-unique 27-block, NO donor (LOW); transmission 22127756 + reservoir 22127749 (22127xxx, no exact cross); no discrete washer-pump P/N in redux (mainstream candidate 22127652/653 = ACDelco 8-6710).

**Fan-out STATE (2026-07-18):** 4 ready PRs await owner merge — #4 mirrors, #5 lamps, #6 HVAC, #7 sweep-r1. Further sweep rounds HELD pending owner review (avoid pileup); ~9 worklist clusters remain (seats/SIR, glass/regulators, latches/locks, interior controls, IPC…). Keyed Parallel_Search stayed unavailable all session (not enabled-in-chat) — all workers used default WebSearch.

Related: [[ev1-lamps-hvac-siblings]] [[ev1-sibling-id-scaled-cases]] [[ev1-sibling-id-methodology]] [[ev1-replica-part-candidates]] [[ev1-replica-brake-build-plan]] [[ev1-umbrella-repo]].
---
