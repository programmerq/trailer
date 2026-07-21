---
name: hardware-purchase-mode-deferred-until-board-order-ready
description: Owner ruling (2026-07-17) — defer ALL EV1 hardware/connector/parts purchases until at least one board reaches low-volume-manufacturer order-ready state; queues stay tracked (evidence-capture only, no buy prompts).
metadata:
  type: project
  modified: 2026-07-18T04:19:59.598Z
---

Owner ruling 2026-07-17 (closing the connector owner-bench decision bundle, backlog `BL-2026-07-16-connector-owner-bench-queue`), verbatim: "I'll switch to hardware purchase mode after we get at least one board to a place where I'm talking about ordering it from a low volume manufacturer. That's a ways out still."

**Why:** the owner is not in buying mode yet — physical procurement waits on the build reaching an order-ready board. Prompting him to buy parts now (even near-extinct thin-stock connectors) is premature and unwanted.

**How to apply:**
- Do NOT surface purchase asks / "buy now before stock lapses" prompts as owner decisions until a board is order-ready. This overrides any earlier triage recommendation to "buy the near-extinct thin stocks now."
- The sourcing queues stay TRACKED: keep capturing part listings / availability evidence and DOCUMENT the vanishing-stock risk for thin single-supply parts in the sourcing catalog — evidence-capture only, no buy prompts.
- Connector keying-deviation calls defer to the same bundle (the btcm-sourcing-queue item points to `BL-2026-07-16-connector-owner-bench-queue`); the standing keying-deviation policy self-derives to case-by-case + provenance-tagged from the parts-strategy preference.
- The bench-bundle decision itself is now RULED/closed; only the tracking work continues.

Related: [[ev1-replica-sourcing-plan]], [[ev1-replica-part-candidates]], [[manual-answerable-findings-source-and-implement]].
