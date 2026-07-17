---
id: 2026-07-17-ux-walkthrough-drive-harness
title: Build the Linux offscreen drive-harness + golden-path drive scripts the ux-walkthrough personas depend on
priority: TBD
status: open
source: load-bearing dependency of decision record 2026-07-16-ux-walkthrough-platform-parity-personas (§4 caveat / §7 open question 6)
created: 2026-07-17
---

## Threshold

The pre-PR `ux-walkthrough` personas can actually run, because a scripted
harness now drives the real `trailer` binary offscreen and emits the per-step
artifact bundle the judge personas consume. Declared pass/fail:

1. **All four golden paths drive headless.** Each of the four golden paths
   (new-from-clipboard; screenshot-acquire; open-image→zoom→navigate;
   new/acquire-with-a-document-open) drives its predetermined click/key sequence
   to completion with no human input, in CI, on the pre-PR offscreen tier — an
   **in-process QTest target built from source** under `QT_QPA_PLATFORM=offscreen`
   (see the two-tier driver split below). The pre-PR tier does not launch or
   consume the prebuilt per-OS dev-build artifact; driving that shipped artifact
   is the real-display / real-Mac milestone tier's job.
2. **A screenshot is captured after every scripted step.** Not one screenshot
   per scenario — one per step, so the walkthrough persona can judge the
   observed result of each action against the four cognitive-walkthrough
   questions.
3. **Each step emits the per-step artifact bundle the persona contract expects.**
   Screenshot + step label + expected-effect, one bundle per step, in the shape
   the §3(A) task-scripted walkthrough persona is written to consume (that block
   is the JUDGE half; this harness is the DRIVE + CAPTURE half).
4. **Wired into the `ux-walkthrough` skill invocation.** The harness is reachable
   from the `ux-walkthrough` skill (a **companion deliverable** — the owner ruled
   §7 Q2 = separate skill, so `.claude/skills/ux-walkthrough/SKILL.md` is being
   built alongside this harness, not a blocked external dependency), so "run the
   walkthrough" produces the bundles rather than being a manual sequence.
5. **The #4 empty→document-open transition is reproduced.** Golden path 4
   actually drives the empty-state→document-open transition and captures the
   toolbar / File-menu state where the New/acquire actions vanish (finding #4) —
   the transition every static-state capture is blind to.

## Context

The [`2026-07-16-ux-walkthrough-platform-parity-personas`](../decision-records/2026-07-16-ux-walkthrough-platform-parity-personas.md)
decision record specifies two goal-driven judge personas (task-scripted
walkthrough + platform-parity), but they are a **spec, not a running gate** until
something drives the real binary through scripted steps and screenshots each step
for the persona to judge. Both persona prompt blocks say so verbatim: the drive
scripts and the offscreen capture harness "do NOT exist today and are currently
unowned" (§3(A) DEPENDENCY note; §4 caveat). This item builds that load-bearing
dependency — the DRIVE + CAPTURE half to the record's JUDGE half — and answers
the record's own §7 open question 6 ("who builds the Linux offscreen
drive-harness + the per-golden-path drive/capture scripts").

Autonomous GUI-clicking is not gate-worthy: §4 marshals the evidence (OSWorld
~60% pass@1 vs. ~72% human; coordinate hallucination on niche apps), so the
chosen approach is a **scripted** harness driving a predetermined sequence, with
the LLM as judge over the captured steps and never as the pixel-clicker. This
item implements exactly that scripted approach.

This harness **sits on top of** the enabling-infra survey
[`2026-07-15-gui-verification-capabilities`](2026-07-15-gui-verification-capabilities.md),
which evaluated candidate real-desktop GUI-verification tiers (headless-X + grab
loops as the cheap Linux default). It does **not** duplicate that survey: it
extends the existing per-change offscreen `QWidget::grab()` UAT capture suite
(`tests/uat/`, AGENTS.md gate G2, `QT_QPA_PLATFORM=offscreen`) with QTest
input-synthesis for the pre-PR tier, and at the milestone tier consumes the
survey's real-window-server path plus its dev-build per-OS artifact as the shipped
binary an external driver drives. The offscreen `grab()` capture method is the
pre-PR substrate; this adds *driving a scripted task through it and capturing
between steps*, without changing the G2 method.

### Scope / deliverable

A harness, delivered in two tiers (see the driver split below):

1. **Pre-PR offscreen tier:** an in-process QTest target **built from source**,
   driving the app's own widgets under `QT_QPA_PLATFORM=offscreen` — extending the
   repo's existing offscreen `grab()` UAT capture suite (`tests/uat/`) with QTest
   input-synthesis. This tier builds a test target; it does **not** consume the
   prebuilt per-OS dev-build artifact.
2. **Real-display / real-Mac milestone tier:** an external driver
   (`xdotool` / `cliclick` / `pyautogui`) driving the **actual shipped dev-build
   artifact** — this is where "drive the prebuilt binary, don't rebuild" (the
   gui-verification survey's delivery mechanism) correctly lives, against a real
   window server.

Both tiers, for each golden path: drive a predetermined click/key sequence,
capture a screenshot after each scripted step, and emit a per-step artifact
bundle — screenshot + step label + expected-effect — that the judge personas
consume.

### The four golden paths to script

From §3(A) of the decision record. Each names its **starting state** and the
**key steps** the drive script must perform:

1. **new-from-clipboard** — start: app open, NO document window (empty state).
   Steps: put an image on the clipboard → invoke new-from-clipboard (and,
   separately, exercise ⌘N per finding #7) → capture the resulting document at
   its default zoom and window size.
2. **screenshot-acquire** — start: app open, NO document window (empty state).
   Steps: invoke the screenshot / acquire action → capture → confirm the captured
   image opens as a document. (The *native* TCC prompt half is real-Mac only —
   see the split table below; offscreen drives Trailer's own explainer text and
   the app-side flow.)
3. **open-image → zoom → navigate** — start: app open, NO document window.
   Steps: open an image file → zoom in and out → step to next / previous image;
   capture each step so the persona can judge whether the zoom-% readout updates
   (finding #5, H1) and navigation works.
4. **new/acquire-with-a-document-open** — start: a document ALREADY open in a
   window. Steps: with the document window open, open the File menu and inspect
   the toolbar → capture the toolbar + menu state. This path exists precisely to
   drive the empty→document-open **transition**: paths 1 and 2 start from the
   empty state and would PASS while finding #4 persists, so #4 is only reproduced
   by driving into the document-open state and re-inspecting the New/acquire
   actions.

### Driver options + recommendation

The two drivers are not competing for one slot — they map cleanly to the two
tiers, because they can drive fundamentally different things. QTest is
**in-process**: it synthesizes events against in-process `QWidget` pointers
linked into a test executable, so it can only drive a target **built from
source** — it cannot launch or drive a separately-built binary or consume a
prebuilt `.tar.gz` / `.dmg`. The external drivers inject real OS input events at
a real display, so they can drive the **actual shipped artifact**.

| Driver | In / out of process | What it can drive | Determinism | Fidelity to real input | Tier |
|---|---|---|---|---|---|
| **Qt Test (`QTest`)** | In-process, linked into a test target | A target **built from source** only (not a prebuilt artifact) | High — synthesized events, no window-server timing | Lower — simulated, not real OS input events | Pre-PR offscreen |
| `xdotool` / `pyautogui` / `cliclick` | External, drives a real display | The **prebuilt shipped dev-build artifact** | Lower — real event queue, timing / focus flakiness | Higher — real user-level input events | Real-display / real-Mac milestone |

**Recommendation: split by tier.**

- **Pre-PR offscreen tier → Qt Test (`QTest`) driving an in-process target built
  from source.** It is in-process and deterministic (synthesized events, no
  window-server race), needs no real display so it runs on the cheap Linux
  offscreen tier, and it **extends the repo's existing offscreen `grab()` UAT
  capture suite** (`tests/uat/`, `QT_QPA_PLATFORM=offscreen`, per AGENTS.md) —
  adding QTest **input-synthesis** (driving clicks / keys) on top of the capture
  the suite already does. Note this input-synthesis is a new-ish extension of that
  suite, not clearly already-present, so it is incremental work rather than
  free. This matches the record's §7 Q3 answer (Qt Test for the offscreen tier).
- **Real-display / real-Mac milestone tier → an external driver
  (`xdotool` / `cliclick` / `pyautogui`) driving the shipped dev-build artifact.**
  Closer to genuine user input against a real window server, which is exactly what
  the milestone fidelity / native-chrome pass needs; its flakiness is acceptable
  there but is why it is the wrong default for a reproducible pre-PR gate.

The determinism-vs-fidelity trade-off the record's §7 open question 3 raises is
therefore resolved *by tier*, not by picking one driver for both.

### What runs pre-PR (offscreen) vs. what needs the milestone real-Mac tier

Reconciling with §4's G2-hybrid ruling — offscreen catches the flow /
interaction / comparison class (the dominant class (b) mode-of-review gap); true
fidelity + native chrome stay real-Mac / manual:

| Scenario / check | Pre-PR offscreen (this harness) | Milestone real-Mac tier |
|---|---|---|
| Window sizing / geometry on open (#2), logical-px | ✅ scripted, observable | ✅ confirm at dpr=2 |
| Menu "New" IA + label comprehension (#3) | ✅ in-window Qt `QMenu` only | ✅ native global menu bar |
| New / acquire vanish across doc-open transition (#4) | ✅ golden path 4 | ✅ native menu-bar slice |
| Missing zoom-level indicator (#5) | ✅ golden path 3 | — |
| ⌘N → new-from-clipboard (#7) | ✅ script ⌘N, assert result | ✅ OS-shortcut clash check |
| Trailer's own permission explainer text (#6) | ✅ read in flow | — |
| HiDPI / dpr=2 render sharpness (#1) | ❌ offscreen is dpr=1 | ✅ real Retina only |
| Native TCC prompt (#6 native half), Dock / Services / native menu chrome | ❌ | ✅ real-Mac / manual only |

The split-line matches §4 exactly: class (b) is fixable pre-PR because the fix is
*executing the task*, not raising fidelity. This harness is the pre-PR offscreen
column only; the real-Mac column is the milestone pass that depends on the
gui-verification survey standing up a real-window-server tier, plus the owner
manual checklist (§5 of the record) for what no agent can verify (#1 Retina
sharpness, the native TCC prompt, OS-level global-shortcut interception, native
chrome).

### Dependencies / open questions

- **Depends on** the `2026-07-15-gui-verification-capabilities` infra — **for the
  milestone tier only**: the real-Mac / real-display tier consumes that survey's
  real-window-server path and its **dev-build per-OS artifact** as the shipped
  binary an external driver drives ("drive the prebuilt binary, don't rebuild").
  The **pre-PR offscreen tier does not depend on the prebuilt artifact** — it
  builds an in-process QTest target from source and extends the existing
  `tests/uat/` offscreen `grab()` suite. That survey item is a *survey*; standing
  up the concrete real-window-server tier is its track, not re-solved here.
- **Co-delivered with** the `ux-walkthrough` skill (owner ruled §7 Q2 = separate
  skill): the skill is a companion deliverable being built alongside this harness,
  not a blocked external dependency. The personas cannot be invoked end-to-end
  until this harness emits the per-step bundles, and the skill wires the two
  together.
- **Open — real-Mac-tier driver (record §7 Q3).** The offscreen tier is settled on
  Qt Test; the specific external driver for the real-Mac tier
  (`xdotool` vs. `cliclick` vs. `pyautogui`) is left to the owner / impl call.

### Priority (proposal — owner to triage)

Frontmatter is `priority: TBD` per the house rule against inventing a rank the
source did not give. **Proposed rank for owner triage: P1** — this harness is the
single unblock for the whole ux-walkthrough / platform-parity persona proposal
(the personas are inert without it), so it gates adoption of the merged decision
record's dominant class (b) coverage. Not P0 (no shipping regression), but the
critical-path enabler for the persona machinery.

## Provenance

Load-bearing follow-up derived from the just-merged decision record
[`docs/decision-records/2026-07-16-ux-walkthrough-platform-parity-personas.md`](../decision-records/2026-07-16-ux-walkthrough-platform-parity-personas.md)
— specifically its §4 "unbuilt dependency" caveat and §7 open question 6 ("who
builds the Linux offscreen drive-harness + the per-golden-path drive / capture
scripts"). Filed 2026-07-17 to give that dependency an owned, closeable item.
