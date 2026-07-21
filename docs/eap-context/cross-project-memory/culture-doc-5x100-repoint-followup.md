---
name: culture-doc-5x100-repoint-followup
description: Pending follow-up — once ev1 PR #11 merges, re-point the "5×100 unconfirmed inference" flags in culture/what_could_have_been.md to the confirmed hub case file.
metadata:
  type: project
---

In `programmerq/ev1`, the counterfactual mod-culture doc `culture/what_could_have_been.md` (shipped in PR #15, branch `claude/culture-what-could-have-been`, commit 431761b) flags the "5×100 hub cross / J-body wheels bolt up" claim as an UNCONFIRMED inference at every occurrence, because as of main @ ceb34a8 the sourcing catalog only documents the wheel as P/N 09592081 (14×4.5 magnesium) with no bolt-pattern printed.

**Pending follow-up (no action until trigger):** ev1 PR #11 (open, ready as of 2026-07-20) carries the CONFIRMED hub case file — hub 7470014, ACDelco 513017 / Timken BR930028K, J/N-body **5×100**, with evidence. **Once PR #11 MERGES to main**, make a tiny follow-up commit on the culture doc that re-points every "5×100 unconfirmed inference" flag to cite that confirmed hub case file (e.g. `sourcing/parts/<hub>.md`) instead of "inference to confirm." If PR #15 is still open, add the commit to its branch; if #15 already merged, open a new small doc-fix PR. The coordinator (session watching #11) is expected to relay when #11 lands.

Related: this is the sourcing sweep's hub cross that the culture panel could not yet cite.
