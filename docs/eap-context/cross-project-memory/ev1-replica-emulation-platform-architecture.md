---
name: ev1-replica-emulation-platform-architecture
description: "Common hardware/firmware platform for the EV1 replica's emulation/translator boxes — MERGED 2026-07-20 (ev1 PR #28): 'Shared-Contract, Federated-by-Fit'; owner adopted all 6 forks"
metadata:
  type: project
  modified: 2026-07-21T13:21:27.327Z
---

Delivered + MERGED 2026-07-20 (ev1 PR #28) — new top-level `electronics/` workstream (`README.md` + `common_platform_study.md`, ~767 lines) + `decisions/2026-07-20-common-emulation-platform.md`. Generalizes the charging translator-ECU pattern to ALL the program's physical emulation/translator boxes so they share ONE platform instead of being invented separately. The 5 boxes: (1) charging EVCC, (2) BPM charging byte-protocol bridge, (3) modern-drive-unit↔faithful-PIM translator, (4) cluster/HMI support ("cockpit-in-a-box"), (5) brake-adjacent electronics.

RECOMMENDATION (owner adopted ALL SIX forks 2026-07-20, verbatim: "Yes, go with the recommendation. We're in early planning so we can always pivot later!") — **"Shared-Contract, Federated-by-Fit"**:
- A portable pure-C **emulation-contract kernel** (GM-8192 framing/sum-check, $41/PRND, charger $E1/$E2, fault-passive machine) that compiles unchanged on every box's MCU AND on host CI — one implementation of the faithful logic, no shared board. (grafts A's single-implementation dividend without a shared board.)
- **Two-MCU-family cap:** house STM32G4 (rides the OpenInverter community) for boxes 1/2/4/5; ONE automotive-grade NXP S32K carve-out for the HV+traction drive-unit translator (box 3). dsPIC33CK / ESP32 opt-in only.
- **One discrete 4-part single-wire PHY cell** (drive / pull / comparator-RX / clamp) covering BOTH the GM-8192 bus AND J1850 VPW — every purpose-built VPW transceiver is EOL; discrete-as-primary gives symbol-level access to reverse-engineer the undocumented BPM Class-2 charging byte protocol (STN2120 interpreter = bench first-light aid only).
- **Virtual EV1 Bus:** adopt commodity EV gear (drive/charge/BMS/PLC) behind ONE versioned internal CAN dictionary — buy the modern side as DATA, never firmware; confines opaque-firmware + obsolescence risk to the modern side of a frozen, model-checked faithful face.
- Connectors: **Deutsch DT size-16 solid-contact family** (DT/DTM + TE AMPSEAL-16, one crimp tool); enclosures Hammond 1550 die-cast (harsh/underhood) + Polycase (cabin). HVIL kept analog & MCU-independent; brake defaults de-energised-to-APPLY; auto-grade silicon matched to hazard.
- Firmware: bare-metal super-loop + timer-ISR; thin per-box HAL; a thin-wiring lint forbids re-implementing kernel logic in a HAL. Every box earns a golden-trace HIL conformance passport (byte-exact + ±0.5% bit / ±5 ms cadence on its own PHY) before touching a real EV1.

HARD PARTS flagged honestly: the J1850 VPW PHY, reverse-engineering the BPM charging byte protocol, and the DC PLC/ISO-15118 stack (AC L1/L2 EVCC is mostly an adoptable known circuit — the resistor-ladder + 1 kHz CP-PWM; DC is the real build). EARLY WINS: cockpit-in-a-box HMI rig + brake HIL flywheel. ACQUISITION-PENDING electrical numbers (GM-8192 absolute rail/pull-up/RX threshold, J1850 VPW PHY, J1773 paddle handshake, master turnaround/inter-byte gaps) resolve into a self-describing calibration EEPROM per box (makes wrong-personality-on-a-safety-box impossible), NOT the sim.

The 6 forks are RESOLVED (not open) in study §5.4 with alternatives preserved as documented pivot options; "early planning — pivots allowed" is the explicit revisit trigger. Built ON the (now also merged) [[ev1-replica-powertrain-direction]] emulation-contract pattern + the NACS-first charging direction. The faithful EV1-facing contracts come from the companion EV1 electrical simulator model's boundary contracts (GM-8192 byte+cadence, Class-2 charger, PHY/timing envelopes, chassis-signal seam, wrong-data taxonomy, HIL bench protocol). Related: [[ev1-umbrella-repo]] [[ev1-replica-brake-build-plan]] [[owner-decision-queue]].
