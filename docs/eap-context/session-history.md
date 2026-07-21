# Project narrative as the coordinator saw it (through 2026-07-21)

What follows is the history that git log does not record: owner interactions,
incidents, and how the process rules came to be. Owner = Jeff Anderson
(programmerq), building Trailer, a C++20/Qt6/qpdf/ONNX cross-platform PDF and
image viewer/editor — "Preview/Acrobat-lite for non-technical users".

## Arc 1 — criteria machinery (2026-07-09..11)
A structured interview produced the design-criteria machinery (gates G1..G9,
decision machinery, milestones) and the review-before-push policy: tests first,
1-2 local review personas, address findings, push, per-item ready-for-review
PRs; merges to main always owner-gated. The remote build recipe (Qt 6.11 via
aqtinstall, ONNX via NuGet) was worked out and automated as a session-setup
hook. A 7-branch criteria batch landed as PR #40 (45/45 green).

## Arc 2 — data integrity P0s (2026-07-12..13)
UAT-FND-014 (closing dirty doc silently discarded) fixed via veto-signal +
confirmCloseDirtyDoc (PR #47, ADR 0004 "never-worry save invariant").
v0.3.0 released 2026-07-13 (PR #50, tag @ f48f99c, linux/macos/windows
artifacts) — first release under the machinery.

## Arc 3 — self-hosted CI + process rulings (2026-07-14..16)
CI moved to self-hosted trailer-k8s runners (later + trailer-small group,
docker via sidecar). Owner rulings that became standing policy:
- PR #74 "oof... too much to have one PR for a doc/proposal" -> no
  proposal/DR-only PRs; DRs merge WITH implementing code; asks are questions.
- ADR numbering collided across parallel branches -> date+slug filenames.
- PR #68 comment -> the surface-the-ask skill (WHAT/CONTEXT/IMPACT, don't
  wait on humans for unambiguous answers), merged as PR #83 after the owner
  said "push" in-session.
- Manual-testing asks = checkbox checklists on the PR; owner checks them off.
- PR #82 closed ("too small... a dev build I may not even consume") ->
  git-derived dev versioning (#84).

## Arc 4 — the macOS dogfood waves (2026-07-16..19)
The owner dogfooded dev builds on his Mac and filed large hands-on feedback
dumps, which became fix sessions:
- TCC/screen-capture: verbose explainer removed ("a manufactured solution to a
  non-problem"), crosshair-vs-explainer ordering, Deny dead-end fixed,
  "cancelled" narration popups removed (-> standing no-narration-dialogs
  taste rule), SCContentSharingPicker researched and shipped flag-gated (#72;
  his local Mac compile caught defaultConfiguration vs configuration).
- File menu IA: Cmd+N = New from Clipboard (his hottest path), Acquire ->
  Screenshot/Scanner/Camera, actions must not vanish when a doc is open,
  non-image paste is a silent noop.
- HiDPI: clipboard/screenshot opens were blurry -> open at logical size,
  1:1 pixel-exact on Retina; dpr {1,1.5,2} UAT matrix (#92).
- Quit semantics: Opt+Cmd+Q always keeps windows (incl. dirty PDFs'
  annotations via SessionDraftStore/StructuralDraft blobs), Cmd+Q always
  prompts per-doc, Option-held menu swap; decoupled from the OS toggle
  (#78, #110, #111).
- Freehand tool: zoom drift, draw-beats-select, latency, sticky-draw;
  "parity" extended the fixes to Rectangle/Line/Arrow.
- "We don't want to lose data, but we don't want to overwrite data either"
  -> P0: Discard must leave the file byte-identical -> recovery-sidecar
  architecture (#90), external-change mtime guard + parent-dir watching
  (#89), deleted-backing hasUnsavedWork semantics (#96).

## Arc 5 — agent-driven UX testing (2026-07-19..20)
tools/ux-walkthrough/ (Xvfb+openbox+xdotool golden paths, screenshot bundles
judged by agent personas) plus the ux-walkthrough skill. First adversarial
audit drove the app for real: 20 raw findings -> 6 confirmed -> all fixed via
routed area-owner sessions. Notable incident: finding CF-4 turned out to be
FABRICATED evidence (md5-identical montage frames); the area owner disputed it
with code+blame+live rerun, the audit re-verified and retracted, and the
machinery (adversarial re-verification) was judged working as intended.

## Arc 6 — the blitz and the minimal-UI pivot (2026-07-20..21)
Owner: "Spin up another round of backlog items... Knock down as many as you
can!" -> 6 parallel sessions, ~16 items -> PRs #99..#114; later an
additive-only lane (#115). The owner rejected #104's progress-bar approach ->
minimal-UI-surface principle (document is the focus; subtle state
glyphs/badges; no narration dialogs/popups/progress bars) codified as
docs/ux-guidelines.md (#116) and applied in reworks of #104 (menu-entry
status glyph), #113 (mode banner -> "Read-only" pill), #114 (checkmark cue).
Two-page/spread view shipped as stacked #112/#113 with adversarial review
rounds (spread-aware navigation bug found by a persona review and fixed).

## Arc 7 — Wayland (2026-07-21)
Owner asked whether Qt speaks Wayland natively (it does — qtwayland, no
XWayland) and whether CI could cover it. Result: #106 honest-degrade for
screenshots (a Wayland "Whole Screen" grab that silently returned nothing was
a lying control), then a headless-sway CI smoke tier (#117 — native wayland
plugin asserted, grim screenshot oracle), then the XDG-portal screenshot
implementation (#118) once CI could host a real Wayland session. Facts: the
aqtinstall Qt bundle ships the wayland plugin; weston lacks wlr-screencopy
(use sway); input injection is impractical headless (no /dev/uinput); 3/60
unit tests fail under real Wayland on non-defects, so units stay offscreen.

## Arc 8 — #110/#111 endgame (2026-07-21, day of export)
Owner: "Merge 110 with a merge commit when able!" -> relayed with authority to
the owning session; merged clean as b5eae1f after gating on head+checks. #111
did NOT auto-retarget (base branch not deleted); its final rebase onto main,
owed backlog-file deletion, retarget, and first-ever real CI (green after a
known Wine QSKIP addition) all completed. The merged branch could not be
deleted from the environment (git relay 403s the delete verb) — left for the
owner's UI click.

## Recurring platform lessons (cost real time; don't relearn)
- The permission classifier ignores relayed authority: the owner must say the
  word in the session that pushes/merges.
- Worker containers die on long turns (~6 deaths one day): checkpoint-push
  per item, verify ls-remote SHA, mark UNPUSHED loudly, takeover sessions
  resume from pushed branches.
- Stacked PRs get ZERO CI (ci.yml keys on PRs->main; base-edit doesn't fire);
  absence of checks != green.
- CI builds the PR merged with main; re-merge main before the final verify on
  ~50-min builds.
- Egress proxy defangs raw URLs >~150 chars in PR bodies (short image names,
  HTML img tags, SHA-pinned; re-pin after force-push).
- Wine tier: unlink-of-open-file crashes are a Wine artifact, not product
  bugs -> runningUnderWine() QSKIP precedent with documented reasons.
