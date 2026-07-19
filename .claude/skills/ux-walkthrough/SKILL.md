---
name: ux-walkthrough
description: Pre-PR UX review gate — run the task-scripted cognitive-walkthrough and platform-parity personas over a user-visible diff's grab() screenshots to catch flow, interaction, and Preview/Acrobat-parity defects the offscreen static-capture reviewers miss.
---

# UX walkthrough

Run this **before every PR whose diff is user-visible** to `programmerq/trailer`,
as a complement to `review-before-push` (it does not replace the
correctness/HIG/frugality reviewers). Where those reviewers ask "is the *changed
state* right?", this pass asks "does the *flow* work, and does it match the
platform-native peer?" — the two axes an offscreen, static, goalless
`QWidget::grab()` substrate is structurally blind to.

It is the implementing artifact of the accepted decision record
[`docs/decision-records/2026-07-16-ux-walkthrough-platform-parity-personas.md`](../../../docs/decision-records/2026-07-16-ux-walkthrough-platform-parity-personas.md),
which diagnoses why a 20-minute manual macOS pass caught seven user-facing issues
(#1–#7) that the machinery (gates G1–G9, the DESIGN lenses, the two-persona
friction audit, the `review-before-push` reviewers) never surfaced. The gap was
never the *lenses* — PR #64 ran the same annoyed-user + UX-expert pair — it was
the **substrate and mode**: offscreen dpr=1, single-state captures, no goal, no
peer baseline. This skill closes the dominant **mode-of-review** class pre-PR.

## When it fires

- **Trigger:** any diff that touches a **user-visible surface** — menus,
  toolbars, windows/geometry, zoom, shortcuts, dialogs, status/empty/error
  states, multi-step flows. This is the Q4 ruling: fire on **all** user-visible
  diffs, not an enumerated subset.
- **Skip:** internal-only diffs with no user-facing surface (same carve-out the
  UX gates G2–G5 use — an internal refactor with no rendered change skips it).
- **Stage:** **pre-PR**, inside the `review-before-push` round. Treat it as one
  additional HIG-adjacent judging pass on top of the existing
  correctness/HIG/frugality reviewers, run only on UX-touching diffs.
- **Cost:** ~2 persona judging passes (one per persona below), each a single
  pass over the diff plus the per-state `grab()` screenshots the diff already
  produces for G2 — order 10⁴–10⁵ tokens total, comparable to one
  `review-before-push` reviewer.

## Staged capability — read before you run

> **TIER-1 HARNESS NOW LANDED (Linux/Xvfb).** The scripted drive + capture
> harness the "full vision" below depended on now exists at
> [`tools/ux-walkthrough/`](../../../tools/ux-walkthrough/README.md). On Linux
> it drives the **real built binary** through the four golden paths under a real
> X server + window manager (`xdotool` input, per-step screenshots), emitting
> the per-step `NN-*.png` + `NN-*.txt` bundles persona (A) consumes — so for the
> **offscreen-observable golden paths, persona (A) can now be driven for real on
> Linux** (`tools/ux-walkthrough/run.sh all`), not only in reduced static mode.
> The macOS/real-Mac fidelity + native-chrome items still stay on the owner
> checklist below. The reduced-mode text that follows remains the honest floor
> for surfaces the Tier-1 harness does not cover (e.g. macOS-only actions).

Be honest about what runs **today** versus what is **gated on unbuilt infra**.
The full vision (a scripted harness that drives the built binary through each
golden path click-by-click and captures a screenshot per step, with the persona
as *judge*) is now realised for the Linux Tier-1 tier by the drive harness at
[`tools/ux-walkthrough/`](../../../tools/ux-walkthrough/README.md) (it replaced
the follow-up backlog item `docs/backlog/2026-07-17-ux-walkthrough-drive-harness.md`,
deleted on close; the Q6 ruling put the harness on the gui-verification track,
not in scope of the decision record). Where the harness does not yet reach —
macOS-only surfaces and real-Mac fidelity — this skill still runs in a
**reduced mode**:

- **Persona (B) platform-parity — OPERATIONAL NOW.** It judges the static
  `grab()` screenshots the diff already produces against Preview/Acrobat
  conventions. No live driving needed. Per the Q7 ruling, for v1 its peer
  conventions are **LLM-recalled + owner spot-check** (no captured peer
  reference required yet), so every recalled convention is marked provisional.
- **Persona (A) task-scripted walkthrough — REDUCED MODE NOW.** Until the
  harness exists it runs as a **static-screenshot cognitive walkthrough** over
  the per-state `grab()` screenshots already captured for the diff — it applies
  the four cognitive-walkthrough questions and the 0–4 severity scale to those
  states, but **does not drive the binary live**. Its full golden-path
  **EXECUTION** (real click/key driving, one capture per step, the empty →
  document-open transition that surfaces finding #4) is **GATED on the
  drive-harness follow-up**. Do not read the prompt block below as a
  paste-and-run gate today: the DEPENDENCY note inside it is live, not
  hypothetical.

So: **B is live; A judges the screenshots you already have, and its full running
gate arrives with the harness.** Nobody should think the complete driving gate
exists today.

## Reconciliation with the G2 hybrid ruling (offscreen / real-Mac split)

This skill slots *inside* the ux-evidence hybrid ruling
(`docs/research/2026-07-13-ux-research-agenda.md`, codified by AGENTS.md gate G2,
cited by ADR-0007/0009) without changing the G2 capture method. Pre-PR /
offscreen catches the **flow / interaction / comparison** class for every surface
the offscreen substrate can render (#2 window geometry as a logical-px assertion,
#3 in-window `QMenu` IA, #4 New/acquire persistence across the doc-open
transition, #5 zoom readout, #6 app-text explainer verbosity, #7 ⌘N binding).

What offscreen **cannot** verify stays on the **owner's manual milestone
checklist** — true Retina/dpr=2 rendering fidelity (#1), the native OS
TCC/permission prompt (#6 native half), OS-level global-shortcut interception
(#7 clash), and Dock/Services/native menu-bar chrome. This record *extends* the
hybrid ruling's real-Mac trigger to add rendering fidelity (#1), which the ruling
had left on the "offscreen suffices" side.

### Owner manual checklist — real-Mac milestone pass (paste-ready)

```
OWNER MANUAL CHECKLIST — real-Mac milestone pass (what no agent can verify)
Run on a real Retina Mac against the milestone dev build.

[ ] #1 HiDPI sharpness (golden path 3: open-image -> zoom): open a known-sharp
    image; at 1:1 on the Retina display it is crisp, not soft. Compare side-by-
    side against Preview at 1:1.
[ ] #6 Native permission prompt (golden path 2: screenshot-acquire): trigger the
    screen-capture flow; the macOS TCC prompt appears and is terse; Trailer's own
    pre-permission explainer preceding it is concise, not verbose.
[ ] #7 Global shortcut (golden path 1: new-from-clipboard): copy an image, press
    Cmd-N; a new-from-clipboard document opens; confirm no OS-level shortcut
    interception or clash.
[ ] Native chrome: Dock icon (light/dark), Services menu entries, and the global
    menu bar render and behave natively for each golden path's surfaces.
[ ] #2 window sizing at dpr=2: confirm the logical-px sizing verified pre-PR also
    reads correctly at Retina scale.
```

## Personas — paste-ready prompt blocks

Run both over the diff and its per-state `grab()` screenshots. Each folds the
verbatim NN/G 0–4 severity scale into its output. These are the two blocks §3 of
the decision record establishes; if you amend them, amend the record too.

### (A) Task-scripted walkthrough persona

```
You are the TASK-SCRIPTED WALKTHROUGH reviewer for Trailer. This block is the
JUDGE half only: a separate scripted harness drives the built binary through the
exact action sequence and captures a screenshot after each step; you consume those
screenshots and judge each step against the cognitive-walkthrough method
(Wharton/Lewis/Polson/Rieman; NN/G). You do NOT read the diff, you do NOT choose
the clicks, and you do NOT look at isolated one-off screenshots.

DEPENDENCY (not yet built): the drive scripts (the exact click/key sequence per
golden path) and the offscreen capture harness do NOT exist today and are
currently unowned. Until they are built, this prompt cannot be pasted-and-run
end-to-end — it is the judge contract those scripts must feed. UNTIL THEN, run
this persona in REDUCED MODE: judge the per-state grab() screenshots the diff
already produced (no live driving), applying the four questions and severity to
those observed states; the full golden-path EXECUTION below is gated on the
drive-harness follow-up.

PRIMARY GOAL (stable end condition): "Get from an intent to a correctly-rendered,
correctly-sized result with no dead ends, using only native conventions."
CONTEXT OF USE: macOS dev build, launched from Finder, used many times per day.
EXPERIENCE / MENTAL MODEL: expects Preview/Acrobat conventions and native macOS
shortcuts.

You are given, per scenario: (a) the goal, (b) the EXACT correct action sequence
(each click/keystroke), and (c) a post-action screenshot captured by the harness
after every step. You did not choose the clicks — the harness drove them; you
JUDGE the observed result.

SUCCESS-CRITERIA ORACLE (declare these BEFORE judging — the G1 threshold your
"correct" verdicts are measured against; never judge "zoom/size looks right" by
taste). For the exact zoom/size rule, use platform-parity persona (B)'s
Preview/Acrobat oracle:
  - Default zoom on open = 1:1 (100%) for an image at or below the viewport, else
    fit-to-window for a larger image.
  - Window size on open = sized 1:1 to the image (or the declared sensible default
    for oversized images), NOT an arbitrary/leftover size.
  - The current zoom % is visible on screen at all times (H1).
  - A shortcut resolves to the action the persona's hot path expects.

GOLDEN PATHS TO WALK (each names its STARTING STATE; script each step; proceed
through the WHOLE task even after a failure so you find every problem, not just
the first):
  1. NEW-FROM-CLIPBOARD (start: app open, NO document window / empty state): copy
     an image to the clipboard -> invoke new-from-clipboard -> confirm a document
     opens at the oracle default zoom AND the oracle window size.
  2. SCREENSHOT-ACQUIRE (start: app open, NO document window / empty state):
     invoke the screenshot/acquire action -> capture -> confirm the captured image
     opens as a document at the oracle zoom/size.
  3. OPEN-IMAGE -> ZOOM -> NAVIGATE (start: app open, NO document window): open an
     image file -> zoom in and out -> step to the next/previous image; confirm the
     zoom % readout updates (H1) and navigation works.
  4. NEW/ACQUIRE-WITH-DOCUMENT-OPEN (start: a document ALREADY open in a window):
     with a document window open, open the File menu and inspect the toolbar ->
     confirm the New action AND the acquire actions (new-from-clipboard,
     screenshot-acquire) are STILL present and reachable, not vanished (finding
     #4). NOTE: golden paths 1 and 2 start from the empty state, so if driven only
     from there they will PASS while #4 persists — this path exists precisely to
     drive the empty -> document-open transition that surfaces it.

FOR EACH STEP, answer the four canonical cognitive-walkthrough questions VERBATIM,
each with PASS or FAIL + one sentence of reasoning against the observed screenshot:
  1. "Will users try to achieve the right result?"
  2. "Will users notice that the correct action is available?"
  3. "Will users associate the correct action with the result they're trying to
      achieve?"
  4. "After the action is performed, will users see that progress is made toward
      the goal?"
A "no" (FAIL) on any question at any step is a logged defect.

FOR EACH STEP output exactly:
  - Scenario + step number and the expected action.
  - What the user must NOTICE at this step (the cue/feedback they rely on).
  - The four questions, each PASS/FAIL + reasoning.
  - Overall step verdict: PASS / FAIL.
  - SEVERITY (verbatim NN/G 0-4 scale; rate FAILs, justify via frequency x impact
    x persistence):
      0 = I don't agree that this is a usability problem at all
      1 = Cosmetic problem only: need not be fixed unless extra time is available
      2 = Minor usability problem: fixing this should be given low priority
      3 = Major usability problem: important to fix, so should be given high priority
      4 = Usability catastrophe: imperative to fix this before product can be released
  - FRICTION NOTE: one line on what the user hits and the fix direction.

End with a table of all FAILs sorted by severity. Do not design fixes in-line; do
not defend the current design. Report only what the observed steps show.
```

### (B) Platform-parity persona

```
You are the PLATFORM-PARITY reviewer for Trailer. Your single job is to compare
each Trailer surface against the macOS platform-native peer apps the user is
migrating from — Preview.app and Adobe Acrobat Reader — and flag every place
Trailer departs from the convention those apps established. You judge against
Nielsen H4 (Consistency & standards):
"follow platform and industry conventions; users shouldn't wonder whether
different words/actions mean the same thing."
(https://www.nngroup.com/articles/ten-usability-heuristics/)

PEER REFERENCE INPUT (required to truly close the comparative gap): you should be
given a CAPTURED reference screenshot of the equivalent Preview.app / Acrobat
surface for each comparison, and you must compare against the CAPTURED peer, not
your memory. If NO captured peer reference is provided for a surface, you MUST
mark that surface's peer convention "LLM-RECALLED (unverified — owner spot-check
required)" and treat the finding as provisional. Recalled conventions can be
wrong; never present one as ground truth. Class (c) is only genuinely closed for
surfaces where the peer was actually captured. (v1 ruling (Q7): captured peer
references are NOT yet required — LLM-recalled + owner spot-check is the accepted
first iteration, so expect to mark every surface provisional until a peer-capture
step is added.)

PRIMARY GOAL (stable end condition): "Use Trailer with Preview/Acrobat muscle
memory intact — every default and shortcut lands where a migrating user expects."
CONTEXT OF USE: a power migrator with strong muscle memory, macOS + Retina.
SUCCESS CRITERIA are comparative and measurable, not vibes.

For EACH surface below, state: (1) the Preview/Acrobat convention (from the
captured reference; mark "LLM-RECALLED (unverified)" if none was provided),
(2) what Trailer does, (3) MATCH / DEPART, (4) if DEPART, the SEVERITY on the
verbatim NN/G 0-4 scale (0 = not a problem ... 4 = usability catastrophe; justify
via frequency x impact x persistence), and (5) the migrating user's concrete
failure at the step where the departure bites.

SURFACES TO COMPARE (add any UX-touching surface the diff introduces):
  - DEFAULT ZOOM on open: peer opens at 1:1 / fit-to-window. Does Trailer?
  - WINDOW SIZING on open: peer sizes the window to the image / sensible default.
    Does Trailer open 1:1 to the image or at an arbitrary size?
  - Cmd-N SEMANTICS: what does the peer bind New to, and does Trailer's Cmd-N
    match the user's hottest path (new-from-clipboard) or a colder one?
  - ZOOM-INDICATOR PRESENCE: peers show a persistent zoom % readout. Does Trailer
    show current zoom anywhere (H1 visibility)?
  - MENU INFORMATION ARCHITECTURE: is each menu label ("New", acquire actions)
    where a Preview/Acrobat user would look, and does it stay reachable once a
    document window is open?
  - PERMISSION-DIALOG VERBOSITY: native macOS TCC prompts are terse. Is Trailer's
    own pre-permission explainer as concise, or verbose relative to the norm?

Output one row per surface: surface | peer convention | Trailer behaviour |
MATCH/DEPART | severity | migrating-user failure. End with the DEPART rows sorted
by severity. Flag only checkable departures that name the convention, the step,
and the failure; drop naked taste.
```

## Disposition

Fold each persona finding into the `review-before-push` disposition step: every
finding gets **fix** (do it now), **justify** (record why acceptable), or
**defer-with-Decision-Record**. Provisional (LLM-recalled) parity findings and
anything on the owner manual checklist above are surfaced to the owner rather
than blocking the PR on an unverified recall.

## Checklist (copyable)

- [ ] Diff is user-visible (menus/toolbars/windows/zoom/shortcuts/dialogs/states/flows). Internal-only → skip.
- [ ] Ran persona (B) platform-parity over the diff's grab() screenshots (operational now; peer conventions LLM-recalled + owner spot-check for v1).
- [ ] Ran persona (A) task-scripted walkthrough in reduced/static mode over the per-state grab() screenshots (full golden-path driving gated on the drive-harness follow-up).
- [ ] Every finding dispositioned: fix / justify / defer-with-Decision-Record (folded into review-before-push).
- [ ] Fidelity/native-chrome items (#1, #6-native, #7-clash, Dock/Services/menu-bar) routed to the owner manual milestone checklist, not asserted pre-PR.
- [ ] Cross-references current: decision record `docs/decision-records/2026-07-16-ux-walkthrough-platform-parity-personas.md` and the Tier-1 drive harness `tools/ux-walkthrough/`.

## References

- Decision record (accepted): [`docs/decision-records/2026-07-16-ux-walkthrough-platform-parity-personas.md`](../../../docs/decision-records/2026-07-16-ux-walkthrough-platform-parity-personas.md) — full diagnosis, the seven findings, the three failure classes, the offscreen/real-Mac scenario table, and the two persona source blocks.
- Drive harness (Q6, gui-verification track), Tier-1 Linux/Xvfb: [`tools/ux-walkthrough/`](../../../tools/ux-walkthrough/README.md) — drives the real binary through the four golden paths and emits the per-step bundles persona (A) consumes. Closed the follow-up backlog item `docs/backlog/2026-07-17-ux-walkthrough-drive-harness.md`.
- Enabling-infra survey: [`docs/backlog/2026-07-15-gui-verification-capabilities.md`](../../../docs/backlog/2026-07-15-gui-verification-capabilities.md).
- Companion gate: [`.claude/skills/review-before-push/SKILL.md`](../review-before-push/SKILL.md) — this skill runs inside its pre-PR round for UX-touching diffs.
