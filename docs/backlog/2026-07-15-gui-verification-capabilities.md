---
id: 2026-07-15-gui-verification-capabilities
title: Survey agent/build capabilities for real-desktop GUI verification (Linux / Windows / macOS tiers)
priority: TBD
status: open
source: owner ask, 2026-07-15
created: 2026-07-15
---

## Threshold

There is a documented, evaluated set of candidate capabilities for running Trailer's
manual-testing / dogfood checklists against a *real* running desktop app on each
target OS — not just headless unit/UAT tests — with a recommendation on which
tier(s) to stand up first.

Declared pass/fail: for each of Linux, Windows, and macOS, the survey names at
least one concrete way to launch the built artifact on a real (or real-enough)
desktop, drive it, and capture screenshots / observe state so a checklist item
can be marked verified; and it states, per candidate, the cost, fidelity
(real window server vs. headless), and setup burden.

## Context / Body

This is a **research / survey** item, not an implementation commitment. The goal
is to let the manual-testing checklists (the dogfood / real-app gates) actually
run on real desktops instead of staying human-only, by surveying what agent and
build infrastructure can drive a live GUI. Candidate directions to evaluate:

- **Linux**: `xvfb` (headless X server) or a real window manager in a container,
  driven in a screenshot loop — launch trailer under a virtual display and use
  Qt's `grab()` / window-server screenshots to capture state between checklist
  steps. Lowest cost, already close to the existing offscreen CI; the open
  question is fidelity vs. a real compositor (and the Wayland path — see
  cross-links below). Evaluate headless-X + grab loops as the cheap default tier.

- **Windows**: a Windows VM runner on the owner's Hyper-V host — a self-hosted
  runner inside a real Windows VM gives a real Win32 window server to drive the
  cross-built `trailer.exe` against, rather than the Wine-offscreen tier CI runs
  today. Evaluate feasibility, maintenance burden, and how a self-hosted Hyper-V
  runner slots next to the existing trailer-k8s pods.

- **macOS**: two candidate tiers.
  - A **self-hosted runner on the owner's own Mac** for a real-Tahoe (current
    macOS) GUI tier — highest fidelity, drives a real Aqua window server on real
    hardware, but ties a checklist run to the owner's machine being available.
  - Note that **GitHub's HOSTED macOS runners CAN drive a real GUI** (they expose
    a real window server, not just offscreen), so batched, opt-in checklist runs
    are possible there too — at the ~10x runner cost, which argues for running
    them batched/gated rather than per-PR.

- **Third-party device farms**: mostly mobile-focused (iOS/Android device clouds);
  desktop-GUI coverage is thin. Flag as **likely insufficient** for this use case,
  but the space isn't fully surveyed — confirm whether any desktop-capable farm
  (real Windows/macOS/Linux desktop sessions, driveable + screenshot-able) exists
  before ruling it out.

### Tie-in / delivery mechanism

The new on-demand **dev-build workflow** (`.github/workflows/dev-build.yml`) is the
delivery mechanism for all of the above: it produces the unsigned per-OS artifacts
(portable Linux tarball, Windows zip, macOS `.dmg`) from any ref *before* merge,
which is exactly what these GUI-verification tiers would download and drive. Any
tier stood up from this survey should consume dev-build's run artifacts as its
input rather than rebuilding. Cross-links: the packaging / artifact work
(`2026-07-13-wire-msi-deb-rpm-packagers`, `2026-07-12-ci-artifact-reuse-github-only`)
and the existing platform-verification items this would automate
(`2026-07-12-g5-real-app-empty-state-run`, `2026-07-12-macos-reopen-realhw-verify`,
`2026-07-12-wayland-screenshot-portal` for the Linux-capture fidelity question).

## Provenance

Owner ask, 2026-07-15. Priority recorded as `TBD` per the "don't invent a
priority" rule — re-triage when picked up. Scoped as a survey: the deliverable is
an evaluated set of candidate directions and a first-tier recommendation, not a
built runner.
