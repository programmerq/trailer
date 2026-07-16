---
id: 2026-07-12-gate-reference-rig-and-corpus
title: Owner ratification of a reference baseline to make budgets B1–B4 binding
priority: P1
status: open
source: criteria-gates session harvest; consolidated docket 2026-07-10
created: 2026-07-12
---

## Threshold

The owner names a reference baseline for the latency budgets — either a named
reference machine, or a ratification of the agent-measured-on-corpus approach as
the baseline — so that performance budgets **B1–B4** stop being advisory and
become binding (a reviewer can run a named budget row against the ratified
baseline and the checked-in corpus and declare pass/fail). Concretely: the
`STATUS: PROPOSED — FOR OWNER RATIFICATION` banner and the
`[advisory until reference rig + corpus ratified]` markers on B1–B4 in
[`../performance-budgets.md`](../performance-budgets.md) are cleared by an owner
ruling. This is a purely owner-level ratification, **not** a build task.

## Context / Body

This item has shrunk to its one surviving fragment: the corpus and the G2
capture-method ruling both landed, leaving only the owner ratification of the
B1–B4 numbers / reference baseline.

Already done (no longer part of this item):

- **Corpus — DONE.** [`../perf/corpus/`](../perf/corpus/) holds ≥3
  representative input files (`form_1page.pdf`, `photo.jpg`,
  `text_20page.pdf`) plus a `README.md` and `generate_corpus.py`. The
  checked-in-corpus requirement is satisfied.
- **G2 capture-method ruling — DONE.** The offscreen-`grab()` ruling landed in
  `AGENTS.md` gate **G2** ("Capture method (ruled)"). That requirement is
  satisfied.

Surviving open work — the only remaining substance:

- No reference machine has been named yet. `docs/performance-budgets.md` is
  still `STATUS: PROPOSED — FOR OWNER RATIFICATION`, and it keeps latency
  budgets **B1–B4** marked `[advisory until reference rig + corpus ratified]`.
  The open task is the owner-level ratification: name a reference machine (or
  ratify the agent-measured approach) so B1–B4 stop being advisory. (B5/B6 are
  already binding — they are corpus-independent wall-clock numbers.)

Important context so a future reader isn't misled about the scope: the owner's
perf-measurement ruling — encoded in `docs/performance-budgets.md` under
*"How the latency budgets are verified"* and consumed by ADR
[`../decision-records/0008-staged-document-open-scheduling.md`](../decision-records/0008-staged-document-open-scheduling.md)
— is that latency budgets are **agent-measured on the reference corpus plus a
reviewer check, with NO CI wall-clock gate**; CI enforces only the
corpus-independent structural invariants. So the surviving fragment is purely
the owner ratification of the B1–B4 numbers / reference baseline. It is **not**
a task to build a CI timing rig — that approach is already ruled out.

Note (2026-07-12): the G2 offscreen-`grab()` ruling landed in `AGENTS.md` gate
**G2** and commit `fd01de0`.

Note (2026-07-15): shrank this item to the surviving ratification fragment. The
checked-in corpus (`docs/perf/corpus/`, ≥3 files + README + generator) and the
G2 capture-method ruling are both done; the only remaining substance is the
owner-level ratification of a reference baseline that clears the advisory marks
on B1–B4. Per the perf-measurement ruling (`docs/performance-budgets.md`
*"How the latency budgets are verified"*, consumed by ADR 0008), no CI
wall-clock rig is in scope.

## Provenance

Criteria-gates work session, harvested into the consolidated follow-up docket
2026-07-10 (P1 item a). Related: perf budget ratification B1–B8 in the owner
decision package.
