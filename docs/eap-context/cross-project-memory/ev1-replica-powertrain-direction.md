---
name: ev1-replica-powertrain-direction
description: Powertrain direction (2026-07-16) — modern hardware (Nissan Leaf drive unit, LiFePO4 pack) with faithful EV1 electronics via a signal/behavior emulation contract; core stays faithful, improvements are opt-in and ledgered.
metadata:
  type: project
---

Powertrain make-vs-buy direction (2026-07-16 owner): MODERN hardware with FAITHFUL electronics.

- A Nissan Leaf drive unit is (per owner's research) very close in shape/size to the EV1 PIM+motor package.
- A LiFePO4 pack can hit ~50% of the original sealed-lead-acid (SLA) pack weight.
- The core engineering task is making the faithful EV1 electronics "see what they expect" from the modern motor and pack — i.e. a signal/behavior EMULATION CONTRACT, not re-creating period hardware.

IMPORTANT project-culture rule: the electricsim CORE stays as faithful as reasonably possible (the car's "soul"). Improvements — an accelerometer the original lacked, a CANbus variant that keeps the original soul, swapping the original connector harness for cheaper commodity connectors — are OPT-IN, clearly ledgered, and deliberately NOT baked into the core.

Links: [[ev1-replica-selective-fidelity]] [[ev1-replica-definition-of-done]].
