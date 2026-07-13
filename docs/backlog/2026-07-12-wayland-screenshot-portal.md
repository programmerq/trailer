---
id: 2026-07-12-wayland-screenshot-portal
title: Wayland screenshots via XDG portal, or disabled + tooltip — never silent null
priority: P3
status: open
source: platform-honesty follow-ups; consolidated docket 2026-07-10
created: 2026-07-12
---

## Threshold

On Wayland, the screenshot/screen-capture affordance either works via the XDG
desktop portal, or is disabled with a tooltip explaining why (per gate G3, no
lying controls). It **never** silently returns null. Verified by exercising the
capture path on Wayland and confirming one of those two outcomes.

## Context / Body

Platform-honesty item: the screenshot picker must not silently fail on Wayland.
Route it through the XDG portal where available; where not, disable the control
and set a `setToolTip(...)` per G3 rather than returning a null result the user
can't diagnose.

## Provenance

Platform-honesty follow-ups, harvested into the consolidated docket 2026-07-10
(P3 platform honesty).
