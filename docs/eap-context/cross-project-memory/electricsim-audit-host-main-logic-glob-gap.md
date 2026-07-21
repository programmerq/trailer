---
name: electricsim-audit-host-main-logic-glob-gap
description: In electricsim, audit_host_main_logic globs BOTH *_host.cpp AND controller.cpp (HOST_GLOBS, scripts/audit_host_main_logic.py:72), so the 10 controller.cpp modules (incl. btcm) ARE gated — a PASS is real thin-wiring evidence. The prior "globs only *_host.cpp / blind spot" claim was stale; corrected 2026-07-19.
metadata:
  type: project
  modified: 2026-07-19T00:00:00.000Z
---

# audit_host_main_logic covers controller.cpp too (prior blind-spot claim was WRONG)

## Correction (2026-07-19)
The earlier version of this memory said `audit_host_main_logic` globs only
`*_host.cpp` and therefore never inspects `controller.cpp`, leaving 10 modules
ungated. **That is no longer true (and the audit was fixed to close it).** This
was caught by the claude[bot] review on **electricsim#325**, which flagged the
same stale claim living in the p-model drift-review docs.

## The current fact
`scripts/audit_host_main_logic.py:72` defines:

```python
HOST_GLOBS = ("*_host.cpp", "controller.cpp")
```

So the audit scans BOTH shapes: the 6 simple `*_host.cpp` supervisors (ad, pscm,
sdm, tjb, aux_battery, hv_bus) AND the 10 per-module `controller.cpp` mains
(apm, bpm, btcm, htcm, ipc, lhjb, pim, rhjb, rsa, scan_tool). The counter treats
`controller.cpp` orchestration (framer-drain, freshness gates, supervisor/observe
delegation) as plumbing, but its `--self-test` proves the anti-gutting path three
ways: an inline control law hiding amid that orchestration — even one that ALSO
pokes the supervisor — still trips the budget.

## Consequence
For the `controller.cpp` modules (BPM/PIM/AD/BTCM among them) a green
`audit_host_main_logic` IS evidence that no inline decision-soup crept into the
host main (AGENTS.md §4, BL-0080). You can rely on the gate; no hand-check of
`controller.cpp` thin-wiring is required just because the module uses it as its
main. (A hand-review remains fine as defence in depth, but the gate is no longer
blind there.)

## Related
[[electricsim-regenerate-scorecards-before-push]]
