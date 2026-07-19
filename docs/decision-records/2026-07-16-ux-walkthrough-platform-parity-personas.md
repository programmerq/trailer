# UX-walkthrough + platform-parity review personas for interaction/flow review

- **Status:** accepted <!-- proposed | accepted | superseded-by <YYYY-MM-DD-slug> -->
- **Arbiter:** the review-machinery arbiter role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-16
- **Date accepted / superseded:** 2026-07-17
- **Builds on / extends:** the ux-evidence hybrid ruling in
  [`docs/research/2026-07-13-ux-research-agenda.md`](../research/2026-07-13-ux-research-agenda.md)
  (lines 18–23), which is **codified by** AGENTS.md gate **G2** ("UX-Done:
  screenshots of every affected state" — its "Capture method (ruled)" sub-bullet
  fixes the offscreen-`grab()` method) and **cited by** ADR-0007 / ADR-0009; and
  the real-desktop GUI-verification survey
  [`docs/backlog/2026-07-15-gui-verification-capabilities.md`](../backlog/2026-07-15-gui-verification-capabilities.md),
  which this record **relies on as enabling infrastructure**. That item only
  *surveys* candidate capabilities — the Linux offscreen drive-harness and the
  per-golden-path drive scripts this proposal needs are **not built yet and are
  currently unowned** — so this record depends on that unbuilt infra rather than
  re-solving the survey (see §4 and the owner-gated questions in §7). This record
  adds a *review mode* (goal-directed task execution and platform comparison); it
  does not change the G2 capture method, and it **extends** the hybrid ruling's
  real-Mac trigger to cover rendering fidelity (see §4).

## Context

### 1. Problem / context — what a 20-minute manual pass caught that the machinery never did

On 2026-07-16 the owner ran a ~20-minute manual pass over a macOS dev build and
surfaced **seven** user-facing UX issues. None had been caught by the automated
review machinery (gates G1–G9, the DESIGN persona lenses, the two-persona
friction audit, or the `review-before-push` reviewers). The seven:

1. Blurry HiDPI image rendering at default zoom (soft on Retina at 1:1).
2. Windows open at an arbitrary size instead of 1:1 to the image.
3. File menu "New" information-architecture confusion (unclear what "New" does).
4. New/acquire actions disappear once a document window is open.
5. No zoom-level indicator anywhere.
6. Overly verbose permission dialog.
7. ⌘N is not mapped to the owner's hottest path (new-from-clipboard).

**Before / After — what the owner saw vs. what the personas reported.**

| | Owner's manual pass (2026-07-16) | Automated personas to date |
|---|---|---|
| **Substrate** | Real macOS dev build, real Aqua window server, Retina display (dpr=2). | Offscreen `QWidget::grab()` under `QT_QPA_PLATFORM=offscreen`, dpr=1, no window server. |
| **Mode** | Pursued concrete goals ("make a new image from my clipboard, right now"), executing multi-step tasks end-to-end. | Read a `git diff` and static single-state captures; inspected surfaces one at a time. |
| **Baseline** | Judged Trailer against Preview/Acrobat muscle memory instantly. | No persona prompted to compare against the platform-native peer. |
| **Result** | 7 findings across rendering fidelity, window geometry, menu IA, a state transition, a missing readout, dialog verbosity, and shortcut ergonomics. | A **disjoint, narrower** set (see PR #64 below). |

**PR #64 is the sharpest evidence.** PR #64 ("UX friction audit") ran the *same
two lenses the owner used by hand* — an **annoyed end user** and a **thoughtful
UX expert**, as separate adversarial reviewers — and swept "every user-facing
surface (dialogs, menus, toolbars, status/empty/error states, multi-step flows),
driving the running app **offscreen** with `grab()` evidence." That pass caught
dark-mode disabled-icon contrast and a single-page crop-checkbox (→ ADR-0012),
and confirmed the search magnifier "already disabled + tooltip." It surfaced
**none** of the seven owner findings. The lenses were fine; the **substrate**
(offscreen dpr=1, static screenshot, goalless) was the ceiling. The same annoyed-
user + UX-expert pair, run through the same offscreen static grab, is
structurally blind to fidelity, flow, and comparison — the three axes where all
seven findings live.

None of the seven is a **gate violation**. The machinery proved exactly what it
was built to prove — the changed state is present, non-lying (G3), native-shaped
(G4), correct empty state (G5), and screenshotted (G2) — and never entered the
axes where the findings sit. Every finding is in a gate/persona **blind spot**.

### 2. Gap analysis — three failure classes

The seven findings partition into three structural failure classes:

- **(a) Environment fidelity.** Offscreen `grab()` renders at
  **devicePixelRatio 1.0** on no real window server, so the HiDPI/Retina
  rendering class is *structurally invisible*. This is the class the
  `2026-07-15-gui-verification-capabilities` backlog survey addresses (real
  window server vs. headless); this record **references it, and does not
  re-solve it**.
- **(b) Mode of review.** Every persona/reviewer consumes a **diff or a static
  single-state grab**; nobody **executes a task end-to-end with a goal**, so
  interaction/flow bugs (vanishing menu items, wrong default zoom, missing
  feedback, wrong shortcut) are out of reach. **This is the class this proposal
  primarily targets.**
- **(c) Missing comparative baseline.** No persona is prompted to compare a
  surface against its **platform-native peer** (Preview / Acrobat). The DESIGN
  "power migrator" lens *encodes* this comparison ("moving from Preview/Acrobat…
  missing keyboard shortcuts or changed terminology") but has only ever been run
  as narration over a Trailer-only static screenshot, never side-by-side against
  the peer app it is defined by.

Each finding mapped to its primary failure class (+ secondary) and the Nielsen
heuristic it violates (heuristic set and mapping from
<https://www.nngroup.com/articles/ten-usability-heuristics/>):

| # | Finding | Primary class | Also | Nielsen heuristic violated |
|---|---|---|---|---|
| 1 | Blurry HiDPI image at 1:1 on Retina | **(a)** fidelity | (c) | **H8 Aesthetic & minimalist design** (renders as a quality/rendering failure); the implicit "sharp as Preview" baseline (c) is never checked |
| 2 | Windows open at arbitrary size, not 1:1 | **(b)** mode | (c) | **H4 Consistency & standards** (Preview/Acrobat open 1:1); reads as **H8** quality failure |
| 3 | File menu "New" IA confusion | **(b)** mode | (c) | **H2 Match between system and real world** (label/IA comprehension); **H4** consistency vs peer menu IA |
| 4 | New/acquire actions vanish with a doc open | **(b)** mode | (a) | **H6 Recognition rather than recall** (a discoverability regression across a state transition); **H1** visibility |
| 5 | No zoom-level indicator | **(c)** baseline | (b) | **H1 Visibility of system status** (user can't tell current zoom %) |
| 6 | Overly verbose permission dialog | **(b)** mode | (c),(a) | **H8 Aesthetic & minimalist design** (exclude irrelevant/rarely-needed info); native TCC prompts are terse (c) |
| 7 | ⌘N not mapped to hottest path (new-from-clipboard) | **(b)** mode | (c) | **H7 Flexibility & efficiency of use** (accelerators for the expert's top task); **H4** consistency |

Heuristic mappings for #4/#5/#7 are drawn verbatim from the research note
(<https://www.nngroup.com/articles/ten-usability-heuristics/>): missing zoom
indicator = *Visibility of system status* (H1); vanishing menu items =
*Recognition rather than recall* (H6); wrong/expert-shortcut mapping =
*Consistency & standards* (H4) / *Flexibility & efficiency* (H7); blurry HiDPI /
non-1:1 window = *Aesthetic & minimalist* quality (H8).

**Aggregate.** Class **(b)** is the primary root of **five of seven** findings
(#2, #3, #4, #6, #7). Class **(a)** is the sole root of #1 (and a contributor to
#4/#6). Class **(c)** spans #2, #3, #5, #7 (and #1/#6) secondarily. The dominant, most
tractable gap is **(b) mode of review** — and it is fixable *inside* the existing
offscreen substrate for most of the affected surfaces, which is why this record
targets it first.

## Options

- **A. Do nothing / rely on manual passes.** Keep offscreen static grab review;
  depend on the owner's periodic HITL passes to catch flow/fidelity/comparison
  bugs. Cheap; leaves classes (a), (b), (c) permanently uncovered between manual
  passes.
- **B. Adopt goal-driven task-scripted + platform-parity personas, run through a
  scripted interaction harness (agent-as-judge), gated pre-PR for the offscreen-
  observable scenarios and batched at milestones on the real-Mac tier for the
  fidelity/native-chrome class.** What this record proposes. Closes class (b)
  pre-PR; routes class (a) and the native-chrome slice of (c)/(6) to the real-Mac
  tier that the gui-verification survey stands up; keeps a short owner manual
  checklist for what no agent can verify.
- **C. Adopt autonomous computer-use GUI agents to drive the app.** Let an agent
  click/type its way through tasks autonomously. Rejected as the *primary* signal
  on reliability grounds (see §4); retained only as an optional exploratory
  second pass.

The proposal is **Option B**. The rest of this record specifies it.

### 3. Proposal — two goal-driven personas

**Why goal-driven beats the current mood-based "annoyed user."** A mood is not a
goal, a task, or a context. Cooper's Goal-Directed Design holds that a persona
must be built on a **stable end condition (a goal)**, with a task being only a
transient step toward it
(<https://www.dubberly.com/articles/alan-cooper-and-the-goal-directed-design-process.html>);
NN/G holds that every persona attribute must **change a design decision**, else
cut it (<https://www.nngroup.com/articles/persona/>). "Annoyed" tells the agent
nothing about *what task to attempt*, *what path to take*, or *what counts as
success* — so it produces vibe-level complaints, not reproducible task failures,
and it carries no **hot paths** to force end-to-end execution. A goal-driven
persona instead states a primary goal (stable end condition), 2–4 hot-path tasks
(explicit step sequences), a context of use, an experience/mental model, and
**measurable success criteria** — which is exactly what turns "the app feels
off" into "step 3: ⌘N did not create a new-from-clipboard document; severity 3."

Two paste-ready persona prompt blocks follow, in the house `review-before-push`
persona style. They fold the heuristic **severity 0–4** scale
(<https://www.nngroup.com/articles/how-to-rate-the-severity-of-usability-problems/>)
into their output.

#### (A) Task-scripted walkthrough persona

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

#### (B) Platform-parity persona

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

### 4. Delivery mechanism — how these personas actually run

**Autonomous GUI agents are not reliable enough to be the gate.** Anthropic's own
computer-use documentation lists the limitations plainly: the model "might make
mistakes or **hallucinate when outputting specific coordinates**," reliability is
"**lower when interacting with niche applications**" (a custom Qt viewer is
niche), and scrolling is flaky
(<https://platform.claude.com/docs/en/docs/build-with-claude/computer-use>).
Benchmark evidence agrees: on OSWorld (real-OS, multi-step tasks) the human
baseline is ~72.4% while state-of-the-art agents reach ~**60% pass@1**, hitting
human level only with pass@5 (five attempts)
(<https://arxiv.org/abs/2404.07972>, <https://arxiv.org/pdf/2510.19949>,
<https://benchmarkingagents.com/osworld/>). At ~60% pass@1, roughly **~2 in 5
tasks fail on a single try** — not acceptable for a reproducible review gate.

**Recommended: a SCRIPTED interaction harness with the LLM as JUDGE, not as the
pixel-clicker.**

- Run the **real built binary** under `xvfb` / offscreen (or, at the milestone
  tier, a real window server), drive a **deterministic, predetermined click/key
  sequence** with **Qt Test / `xdotool` / `pyautogui` / `cliclick`**, and
  **capture a screenshot after each scripted step**.
- Feed those per-step screenshots to the persona as the **observer/judge** against
  the cognitive-walkthrough questions and the heuristics — never asking the agent
  to choose the clicks. This sidesteps exactly the failure modes Anthropic lists
  (coordinate hallucination, niche-app unreliability, scroll flakiness) while
  still forcing end-to-end *performance* of the task. Reproducibility matters for
  a review gate; pass@5 is not a guarantee.
- **Autonomous computer-use is retained only as an optional exploratory second
  pass**, in its documented sweet spot ("automated software testing… where speed
  isn't critical, in trusted environments"), always with the observe-and-verify
  discipline ("after each step, take a screenshot and verify the outcome"). It
  never gates.

The intended enabling infrastructure is the
[`2026-07-15-gui-verification-capabilities`](../backlog/2026-07-15-gui-verification-capabilities.md)
survey: its Linux `xvfb`/real-WM-in-container + grab-loop tier is the intended
cheap default this harness would run on pre-PR, and its macOS self-hosted /
hosted-runner tiers are the real-window-server substrate the milestone pass needs.
**Caveat (readiness):** that item is a *survey*, not built infrastructure — the
Linux offscreen drive-harness and the per-golden-path click/key drive scripts this
proposal needs **do not exist yet and are currently unowned**. So the pre-PR skill
carries a real, unbuilt dependency; adopting the personas is not "free" until that
harness + scripts are built (see the owner-gated question in §7).

**Reconciliation with the ux-evidence hybrid ruling.** The hybrid ruling
(`docs/research/2026-07-13-ux-research-agenda.md:18–23`, codified by AGENTS.md gate
G2 and cited by ADR-0007/0009) splits evidence into: (1) **default tier** — per-change offscreen
`QWidget::grab()` under `QT_QPA_PLATFORM=offscreen`, which "suffices for most
states"; and (2) **real-Mac tier** — batched passes for **native-chrome / menu /
icon / permission** surfaces `grab()` structurally cannot observe (Dock, Services
menu, TCC prompts). This proposal slots *inside* that split without changing it:

**Recommendation: add a new lightweight skill `ux-walkthrough`, invoked PRE-PR for
UX-touching diffs, PLUS a milestone-batch real-Mac task-scripted pass.**

- **`ux-walkthrough` (pre-PR, default/offscreen tier).** For any UX-touching
  diff, run the scripted golden-path scenarios *that can run offscreen* plus the
  **platform-parity persona** on the diff/screenshots. It complements
  `review-before-push` (it does not replace the correctness/HIG/frugality
  reviewers); think of it as the "does the *flow* work" pass to their "is the
  *changed state* right" pass.
- **Milestone real-Mac task-scripted pass (real-Mac tier).** The fuller task-
  scripted walkthrough runs batched on the real-Mac tier for the fidelity /
  native-chrome class.

**Concretely, which scenarios run where:**

| Scenario / check | Pre-PR offscreen (`ux-walkthrough`) | Milestone real-Mac tier |
|---|---|---|
| Window sizing / geometry on open (#2) — logical-px assertion | ✅ scripted, observable offscreen | ✅ confirm at dpr=2 |
| Menu "New" IA + label comprehension (#3) | ✅ but judged on the **in-window Qt `QMenu`** rendering only (Linux offscreen) | ✅ confirm on the macOS **native global menu bar** (offscreen cannot render it) |
| New/acquire vanish across doc-open transition (#4) | ✅ open doc, then look for New/acquire | ✅ confirm native menu-bar slice |
| Missing zoom-level indicator (#5) | ✅ zoom, look for the readout | — |
| ⌘N bound to new-from-clipboard (#7) | ✅ script ⌘N, assert the resulting document | ✅ confirm no OS-level shortcut clash |
| Trailer's OWN permission explainer verbosity (#6, app text) | ✅ read the explainer in flow | — |
| HiDPI / dpr=2 render sharpness (#1) | ❌ offscreen is dpr=1 — structurally invisible | ✅ real Retina only |
| Real OS TCC / permission prompt (#6, native prompt) | ❌ | ✅ real-Mac only |
| OS-level global shortcuts, Dock / Services / native menu chrome | ❌ | ✅ real-Mac only |

The split-line is deliberate: **class (b) mode-of-review is fixable pre-PR** for
every surface the offscreen substrate can render, because the fix is *executing
the task*, not increasing fidelity. Only class (a) fidelity and the native-chrome
slice need the real-Mac tier. This is **not** merely the boundary the hybrid
ruling already draws: that ruling splits on *native-chrome observability*
(Dock / Services / TCC) and explicitly left HiDPI **rendering fidelity** on the
"offscreen `grab()` suffices" side — its known blind spot. This record
**extends** the real-Mac trigger to include rendering fidelity at dpr=2
(finding #1), closing the gap the ruling left open, in addition to the *flow*
axis it adds pre-PR.

### 5. Boundary — what stays impossible without real hardware

Be honest about the ceiling. No agent — scripted-judge or autonomous — and no
offscreen harness can verify the following; they belong on the **owner's manual
checklist at milestones**:

- **True Retina / dpr=2 rendering fidelity** (finding #1 blur). Offscreen `grab()`
  is dpr=1 by construction; sharpness at true device pixels is real-hardware only.
- **OS-level TCC / permission prompts** (finding #6, the *native* prompt half).
  Trailer's own explainer text is offscreen-observable; the macOS system prompt
  is real-Mac only, per the hybrid ruling.
- **OS-level global keyboard shortcuts** — whether a binding is intercepted or
  clashes at the OS layer.
- **Dock / Services / native menu-bar chrome** — the surfaces the hybrid ruling
  already reserves for the real-Mac tier.

Paste-ready owner manual checklist, keyed to the golden paths:

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

### 6. Cost estimate per PR

Expressed as **agent count + complexity**, not wall-clock:

- **Pre-PR `ux-walkthrough` (per UX-touching diff): ~2 persona agents.**
  (1) task-scripted walkthrough — *offscreen scenarios only* — and (2) platform-
  parity persona on the diff/screenshots. Each is a **single judging pass** over
  a diff plus a bounded set of per-step screenshots (roughly one screenshot per
  scripted step across ≤3 short golden paths), so token cost is **moderate — low
  tens of thousands of tokens per persona, order 10⁴–10⁵ total**, comparable to
  one `review-before-push` reviewer pass. It adds one HIG-adjacent pass on top of
  the existing correctness/HIG/frugality reviewers, and only on diffs that touch a
  user-facing surface (internal-only diffs skip it, same as the UX gates).
- **Milestone real-Mac batch: the fuller scripted run.** All golden-path scenarios
  driven on the real-Mac tier — order **one walkthrough agent per scenario (≤3)
  plus one parity pass**, ~4 judging passes, on the higher-cost real-window-server
  runner (the gui-verification survey notes hosted macOS runners at ~10× cost, so
  this tier is batched/gated, never per-PR) — plus the owner's manual checklist
  above for the class no agent can verify. Complexity is higher (real binary, real
  display, per-step capture) but frequency is low (milestones, not per-PR).
- **Not counted above:** these figures cover only the persona **judge** passes.
  They exclude the harness **drive + capture** cost — building and maintaining the
  drive scripts, the CI minutes to launch the real binary per run, and storing one
  screenshot per scripted step. That cost lands on the gui-verification track, not
  the persona budget, and is part of the unbuilt dependency flagged in §4.

## Personas debate

Per the DESIGN §2.5.2 lenses, on **whether to adopt Option B**:

- **Office non-technical user:** Doesn't care how review runs, but is the direct
  beneficiary — the vanishing New/acquire actions (#4) and the "what does New do?"
  confusion (#3) are exactly her anxieties ("Where did my action go?"). Favours B:
  a flow-executing reviewer is the only one that would have hit her path.
- **Older careful user:** Wants to know the app's state at all times; the missing
  zoom indicator (#5, H1 visibility) is her concern. Favours B; neutral on the
  harness mechanics so long as the readout gets checked.
- **Power migrator:** The strongest stake. #2 (1:1 window), #3 (menu IA), #5 (zoom
  %), #7 (⌘N) are all Preview/Acrobat-muscle-memory departures. The parity persona
  (B) makes her lens executable — and genuinely *comparative* only once a captured
  peer reference is supplied; without it (B) falls back to LLM-recalled conventions
  that need owner spot-check. Either way it is a step past the DESIGN
  power-migrator lens that until now only narrated a Trailer-only screenshot.
  Strongly favours B.
- **Occasional user:** Forgets everything between sessions, so relies on
  recognition over recall (#4, H6) and visible feedback. Favours B; the cognitive-
  walkthrough method is literally a learnability instrument for exactly her.

## Admissible objections

- **Power migrator, "compare against the peer" step:** running the migrator lens
  as narration over a single Trailer-only static screenshot never surfaces a
  *departure* from Preview/Acrobat, because the peer is never in frame — so #2,
  #3, #5, #7 pass review and fail the user. Named, decisive: motivates persona (B).
- **Occasional/office user, the "open a document then make a new one" step:** no
  reviewer drives the empty→document-open transition, so the New/acquire
  discoverability regression (#4) is invisible to every static-state capture (G5
  tests only the empty state). Named, decisive: motivates persona (A)'s
  transition-walking.
- **Any HiDPI user, the "view at 1:1" step (#1):** offscreen dpr=1 cannot render
  the softness, so no pre-PR pass can catch it. This objection is *admissible but
  unfixable pre-PR* — it is why §5 routes #1 to the owner manual checklist, not to
  the skill.

### Rejected as naked preference

- "Just use an autonomous computer-use agent, it's simpler." — rejected: states no
  user/step/failure and ignores the reliability evidence (§4, OSWorld ~60% pass@1);
  a non-reproducible gate is not a gate.
- "Personas already exist, don't add more." — rejected: names no concrete failure;
  the existing personas demonstrably (PR #64) miss the whole (b)/(c) class — the
  gap is the *substrate and mode*, not the number of lenses.

## Checkable threshold this record would establish

If accepted, the pass/fail lines an agent or reviewer can independently declare:

1. A skill `.claude/skills/ux-walkthrough/SKILL.md` exists and is invoked for
   UX-touching diffs, running (a) the task-scripted walkthrough over the
   offscreen-runnable golden paths and (b) the platform-parity persona, each
   emitting per-step/per-surface findings with a verbatim 0–4 severity.
2. The two persona prompt blocks in §3 are the ones the skill uses (or supersedes
   them by an amended record).
3. The scripted harness drives the real binary and captures a screenshot per step;
   the persona judges screenshots and does not choose clicks (no autonomous pixel-
   clicking in the gating path).
4. The offscreen-vs-real-Mac scenario split in §4's table is honoured: #1 and the
   native-permission/global-shortcut/native-chrome checks are on the owner manual
   checklist (§5), not asserted by the pre-PR skill.
5. Nothing in this record changes the G2 capture method or the hybrid ruling's
   offscreen/real-Mac split; it adds a review *mode* inside them.

## Arbiter verdict + rationale

**Verdict (2026-07-17): ACCEPTED — Option B.** The owner (programmerq) ratified
the two owner-gated machinery questions in-session; the remaining five were
derived from already-recorded rulings (the G2 capture-method sub-bullet, the
ux-evidence hybrid ruling, and the `review-before-push` trigger scope) per the
`decision-brief` self-decide discipline. Resolutions to the §7 questions:

1. **Adopt (Q1).** Ratified by the owner in-session. Adopt Option B — the two
   goal-driven personas plus a pre-PR `ux-walkthrough` skill for the
   offscreen-observable flow/comparison class, plus the milestone real-Mac
   task-scripted pass for the fidelity/native-chrome class.
2. **Separate skill (Q2).** Ratified by the owner in-session. `ux-walkthrough`
   is a **separate** skill invoked alongside `review-before-push`, not a fourth
   persona folded into that skill's reviewer set.
3. **Qt Test (Q3).** Standardise the drive harness on **Qt Test** — in-tree,
   deterministic, no display required — for the offscreen pre-PR tier.
   Determinism is the priority for a reproducible gate; the real-input drivers
   (`xdotool`/`pyautogui`/`cliclick`) are reserved for the real-Mac tier where a
   display exists. Derived from the G2 offscreen capture-method ruling.
4. **All user-visible diffs (Q4).** The pre-PR pass fires on **every**
   user-visible / UX-touching diff — not an enumerated surface subset —
   skipping internal-only diffs, mirroring the existing UX-gate (G2–G5)
   carve-out and the `review-before-push` trigger scope. The ~2-agent cost (§6)
   is accepted at that frequency.
5. **Split confirmed (Q5).** The offscreen/real-Mac scenario split in §4's table
   is confirmed as written — in particular, window sizing (#2) is accepted as a
   **logical-px pre-PR assertion** with a real-Mac dpr=2 confirmation, not
   real-Mac-only. Consistent with the hybrid ruling this record extends.
6. **Harness is a follow-up (Q6).** Building the Linux offscreen drive-harness +
   the per-golden-path drive/capture scripts is **out of scope of this record**
   and tracked as a follow-up on the gui-verification track:
   [`tools/ux-walkthrough/`](../../tools/ux-walkthrough/README.md) (the Tier-1
   Linux harness that closed the follow-up backlog item
   `2026-07-17-ux-walkthrough-drive-harness`).
   Persona (A)'s full golden-path **execution** (live driving, one capture per
   step, the empty→document-open transition for #4) remains **gated on that
   follow-up**; until it lands, (A) runs in reduced static-screenshot mode.
7. **LLM-recalled + owner spot-check for v1 (Q7).** Persona (B) is **not**
   required to receive captured Preview/Acrobat reference screenshots for the
   first iteration. Recalled peer conventions are marked provisional
   ("LLM-RECALLED — owner spot-check required"); a captured-peer step that truly
   closes class (c) is a later refinement, not a v1 blocker.

**Implementing artifact:** [`.claude/skills/ux-walkthrough/SKILL.md`](../../.claude/skills/ux-walkthrough/SKILL.md).
It carries both persona blocks from §3, the when-it-fires trigger (Q4), the
offscreen/real-Mac split and §5 owner manual checklist (Q5), and the
staged-capability boundary — persona (B) operational now over static `grab()`
captures, persona (A) in reduced static mode with its full golden-path
**execution gated on the drive-harness follow-up (Q6)**. Q3 (driver = Qt Test
for the offscreen tier) is recorded here and deferred to the harness follow-up
for implementation; the skill deliberately names no driver, since the driver
lives in that follow-up, not in the judge-side skill. Per the
review-before-push standing policy, this record's status flips to `accepted`
because the implementing artifact lands in the same change.

## 7. Recommendation summary + open questions for the owner

**Adopt now (no new *real-Mac* infra needed — BUT requires the Linux offscreen
drive-harness + golden-path drive/capture scripts to be built first; these do not
exist yet and are currently unowned):**

- Add the **`ux-walkthrough` skill** invoked pre-PR for UX-touching diffs, running
  the **task-scripted walkthrough** (offscreen-runnable golden paths) + the
  **platform-parity persona** (§3), each with 0–4 severity. This closes the
  dominant class (b) mode-of-review gap for #2, #3, #4, #5, #6(app-text), #7.
- Replace the mood-based "annoyed user" framing with **goal-driven personas**
  (primary goal + hot paths + context + measurable success criteria).

**Needs the gui-verification infra first (real window server / real-Mac tier):**

- The **milestone real-Mac task-scripted pass** for HiDPI fidelity (#1), the
  native TCC prompt (#6), OS-level shortcuts (#7 clash check), and native chrome.
  This depends on standing up the `2026-07-15-gui-verification-capabilities`
  tiers; this record consumes that survey, does not re-solve it.

**Stays manual (owner checklist, §5) — impossible for any agent/offscreen harness:**

- True Retina/dpr=2 sharpness (#1), the native OS permission prompt (#6 native
  half), OS-level global-shortcut interception, and Dock/Services/native menu-bar
  chrome.

**Owner-gated decisions:**

1. Adopt Option B (the two personas + `ux-walkthrough` skill + milestone pass), or
   defer pending the gui-verification survey landing first?
2. Should `ux-walkthrough` be a **separate** skill invoked alongside
   `review-before-push`, or a new **persona (4)** folded into that skill's
   reviewer set?
3. Which harness driver to standardise on for the scripted sequences — **Qt Test**
   (in-tree, deterministic, no display) vs. `xdotool`/`pyautogui`/`cliclick` (real
   input events, needs a display)? Trade-off is determinism vs. fidelity.
4. Is the pre-PR ~2-agent cost (§6) acceptable on every UX-touching diff, or should
   it fire only on diffs touching an enumerated set of surfaces (menus, windows,
   zoom, shortcuts, permission dialogs)?
5. Confirm the offscreen/real-Mac scenario split in §4 — in particular that window
   sizing (#2) is accepted as a *logical-px* pre-PR assertion with a real-Mac
   dpr=2 confirmation, rather than real-Mac only.
6. Who builds the Linux offscreen **drive-harness + the per-golden-path
   drive/capture scripts** the pre-PR skill depends on — is that in scope for this
   record, or a follow-up on the `2026-07-15-gui-verification-capabilities` track?
   The personas cannot run pre-PR until this exists (see §4 caveat).
7. Should persona (B) be **required to receive captured Preview/Acrobat reference
   screenshots** (so class (c) is truly closed), or is LLM-recalled-plus-owner-
   spot-check acceptable for the first iteration?

## Evidence required to reopen

Once accepted: a measured case where the `ux-walkthrough` skill either (a) misses
a flow/comparison defect a manual pass then catches on an offscreen-observable
surface (indicating the golden paths or persona prompts under-specify the flow),
or (b) blocks on false positives frequent enough to burden UX-touching PRs — plus
owner sign-off. A change to the offscreen/real-Mac split would additionally
require superseding evidence against the hybrid ruling in
`docs/research/2026-07-13-ux-research-agenda.md`.
