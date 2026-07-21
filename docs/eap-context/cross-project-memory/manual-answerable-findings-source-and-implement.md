---
name: manual-answerable-findings-source-and-implement
description: Owner rule — findings answerable from the service manual get adversarially sourced and implemented, not surfaced as owner decisions.
metadata:
  type: feedback
---

Owner ruling 2026-07-09 (electricsim). When a finding is answerable from the service manual, adversarially source it — independent agents read the primary scans/redux and try to refute each other's reading (the errata-verify pattern) — and implement to the sourced answer, including any downstream refactor (scenarios, servo contracts, tests). Do NOT surface it as an owner decision. Only escalate: (a) items still genuinely unclear after that verification, or (b) true engineered-choice questions where no documentation exists either way (e.g. the charger coupler-removal case before the CHRG-STOP evidence was found).

**Why:** the owner wants far fewer decisions tossed his way; documented behavior gets implemented, period. His words on an over-escalated find: "This sounds like you're asking permission to implement something clearly in the manual. What's the ask here?"

**How to apply:** default to source+implement for manual-grounded findings; reserve owner asks for real post-verification ambiguity or genuine no-documentation engineered choices. Safety-critical location means careful implementation with tests + provenance, not a permission request. Applies to all findings, not just the P formal-methods workstream.
