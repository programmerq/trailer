# 0014 — Reconciling the UX-recorder Screen-Recording gate with #59's screenshot-import explainer

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-16
- **Date accepted / superseded:** 2026-07-16 (accepted)

## Context

A recorder-enabled build (`-DTRAILER_ENABLE_UX_RECORDER=ON`,
`TRAILER_UX_RECORDER` defined) now contains **two independent flows that both
ask the user about the same macOS "Screen Recording" privacy permission**, worded
differently and suppressed independently. This record settles how those two flows
should be reconciled. It is filed by, and its implementation lands in, the same PR
that introduces the recorder (`feat/ux-recorder`); the code is not written yet, so
this record is **proposed**, not accepted.

**Mechanism A — the screenshot-import explainer (main, PR #59).**
`src/platform/ScreenCapturePermission.{h,cpp}` shows a one-time informational
`QMessageBox` ("Screen Recording Permission" / "Trailer is about to capture your
screen to import a screenshot") the first time the *screenshot-import* feature is
used. It is **not** a TCC call — it explains that macOS labels a still capture
"Screen Recording"; the real OS prompt fires later when `screencapture` runs. It is
gated by a persistent Settings first-use flag, `screen_capture_explainer`
(`ScreenCapturePermission.h:14`), queried by `shouldShowScreenCaptureExplainer`
(`ScreenCapturePermission.cpp:12-17`) and burned only on Continue by
`acknowledgeScreenCaptureExplainer` (`:19-26`). It is shown lazily via
`maybeShowScreenCaptureExplainer` (`:29-58`) from Take Screenshot
(`src/ui/MainWindow.cpp:2272`) and Acquire from Screenshot
(`src/app/Application.cpp:434`). It is compiled under `Q_OS_MACOS` and ships in
**every** build, recorder or not.

**Mechanism B — the recorder's launch-time TCC gate (this branch).**
`Application::preflightUxRecording` (`src/app/Application.cpp:96`) is a **real** TCC
gate. It calls `trailer::uxScreenRecordingGranted()`
(`CGPreflightScreenCaptureAccess`, `src/uxrecord/MacUxPlatformCapture.mm:1002-1006`;
the non-mac stub returns granted, `src/uxrecord/StubUxPlatformCapture.cpp:50-53`).
When not granted it shows a blocking dialog ("UX Recorder — Screen Recording not
enabled") offering **Open Settings & Quit** (which calls
`uxRequestScreenRecording()` → `CGRequestScreenCaptureAccess`,
`MacUxPlatformCapture.mm:1014-1021`, then deep-links the Screen Recording pane and
quits), **Record Without Screen** (proceed degraded), or **Don't Record This
Launch**. It runs at startup from `src/main.cpp:47`, **only** in
`TRAILER_UX_RECORDER` builds, and is gated purely by **live TCC state** — there is
no Settings suppression flag; once the OS reports granted, the dialog never appears.

**The problem.** Same app, same single macOS "Screen Recording" grant, two dialogs
with **independent** suppression:

- A recorder build that has never been granted Screen Recording can prompt the user
  **twice** — once at launch (Mechanism B) and again on the first Take Screenshot /
  ⌘⇧3 (Mechanism A) — because A's `screen_capture_explainer` flag and B's live-TCC
  check know nothing about each other.
- Acknowledging one **never** suppresses the other: continuing past A's explainer
  sets the Settings flag but leaves B's launch gate to fire on the next launch;
  granting the OS permission through B's Open-Settings path clears B's gate but
  leaves A's explainer to fire on first screenshot.
- The two dialogs describe the **same permission in inconsistent language** ("does
  not record video — only the image you select" vs. "record your input and camera
  but no screen"), on a **privacy-sensitive surface** where mixed messaging about
  what is and isn't being captured is exactly the wrong place to be inconsistent.

This is the double-prompt / split-suppression defect this record exists to close.

## Options

- **A. Keep both flows fully independent (do nothing).** Ship the recorder with
  Mechanism B's launch gate and Mechanism A's explainer untouched. Zero new code;
  retained as the null baseline this record measures against. Produces the
  double-prompt and inconsistent wording described above.
- **B. Make B the authoritative Screen-Recording surface for recorder builds and
  share suppression state (this record's proposal).** Keep Mechanism B's
  launch-time, relaunch-aware TCC gate as the primary Screen-Recording surface in
  recorder builds, and have Mechanism A **defer to live TCC state and to B's
  acknowledgement** so the two never double-prompt: `maybeShowScreenCaptureExplainer`
  early-returns (proceeds straight to capture) when live TCC already reports Screen
  Recording granted, both flows consult one shared "already explained/granted"
  signal, and the two dialogs are re-worded to read consistently. Default
  (non-recorder) builds are untouched because B is compiled out.
- **C. Drop #59's explainer in recorder builds only.** Compile
  `maybeShowScreenCaptureExplainer` out (or make it a no-op) when
  `TRAILER_UX_RECORDER` is defined, letting B's launch gate be the sole
  Screen-Recording dialog. Removes the double-prompt but loses the screenshot-import
  explainer's specific "still image, not video" reassurance in recorder builds.
  Recorded as the viable fallback to B.
- **D. Fully merge both into one permission class.** Collapse A and B into a single
  Screen-Recording permission component owning both the launch gate and the
  screenshot explainer. Cleanest end state but a larger refactor that touches #59's
  shipped, non-recorder path; deferred.

## Personas debate

- **Office non-technical user:** Just wants to record a session or grab a
  screenshot without being interrogated. Two differently-worded permission dialogs
  for what they experience as "the screen thing" reads as the app being unsure of
  itself — and the wording clash ("only the image you select" vs. "your input and
  camera") is exactly the kind of privacy mixed-signal that makes them hesitate.
  Favours anything that prompts **once** and speaks with one voice — B or C over A.
- **Older careful user:** Reads permission dialogs closely and is alarmed by
  inconsistency about what is captured. Being asked twice about the same "Screen
  Recording" permission reads as "did the first one not take? is something wrong?"
  Most harmed by A. Reassured by B, which prompts once and (via the shared signal)
  doesn't re-ask after they've dealt with it, provided the surviving wording is
  truthful about the still-vs-video distinction.
- **Power migrator (ex-Preview/Acrobat):** Knows the macOS "grant applies to the
  next launch" trap firsthand and expects an app to handle it — which is exactly
  what B's relaunch-aware gate does and A's lazy explainer does not. This lens most
  favours **keeping B authoritative**; a plain explainer that fires after the fact
  and doesn't survive the relaunch quirk looks naive by comparison.
- **Occasional user:** Uses the recorder rarely, if ever, and mostly just takes
  screenshots. Their build may well be the default one, where B doesn't exist — so
  for them nothing should change from #59. Their only stake is that the recorder
  reconciliation must **not** regress the non-recorder screenshot explainer.

## Admissible objections

Each names a user/persona, a step in a real flow, and the failure that user hits.

- **Office / older-careful user, Option A, double-prompt step.** On a fresh
  recorder build that has never been granted Screen Recording: at launch B's dialog
  fires; then on the first Take Screenshot A's explainer fires — two prompts about
  one permission, in inconsistent language. This is the decisive failure against A
  and the reason the record is open.
- **Older-careful user, Option A, split-suppression step.** Having granted the OS
  permission through B's Open-Settings-&-Quit path and relaunched, the user takes a
  screenshot and is *still* shown A's explainer because `screen_capture_explainer`
  was never set — the app re-asks about a permission the user already granted.
  Decisive against A; the reason B's proposal shares suppression state and has A
  early-return on live-granted TCC.
- **Occasional / office user, Option C or D, non-recorder regression step.** If the
  reconciliation removes or refactors A's explainer in a way that leaks into the
  **default** build, a plain screenshot user loses #59's "macOS calls this Screen
  Recording even for a still image" reassurance and hits a bare OS prompt with no
  context. Admissible; it is the constraint that keeps the whole change behind
  `TRAILER_UX_RECORDER` and forbids C/D from touching the shipped non-recorder path.
- **Power-migrator, Option C, lost-reassurance step (minor).** In a recorder build,
  C drops A's explainer entirely, so the first screenshot goes straight to the OS
  prompt without the still-vs-video reassurance. Real but minor — B keeps that
  reassurance where TCC isn't yet granted, which is why B is preferred to C.

### Rejected as naked preference

- "One dialog is just cleaner, merge them." — rejected as stated: names no user,
  step, or failure. The admissible form is the office/older-careful double-prompt
  and split-suppression failures above, which point at B (share state, prompt once),
  not necessarily at D's full merge.
- "The recorder is dev-only, don't bother reconciling." — rejected: the double
  prompt and the privacy-wording clash are concrete failures a real recorder user
  hits on first run; "it's only dev builds" names no reason the failure is
  acceptable.

## Checkable threshold this record would establish

Phrased so a reviewer can independently declare pass/fail on the implementing PR.

- **G14.1 — One prompt per grant in recorder builds.** In a `TRAILER_UX_RECORDER`
  build with Screen Recording **not** yet granted, a first run that reaches both the
  launch gate and a Take Screenshot / Acquire-from-Screenshot action shows **at most
  one** Trailer-authored Screen-Recording dialog for that permission, not two.
- **G14.2 — Shared suppression / live-TCC deference.** Once Screen Recording is
  live-granted (`uxScreenRecordingGranted()` true) **or** the explainer has been
  acknowledged, neither flow re-prompts: `maybeShowScreenCaptureExplainer`
  early-returns `true` (proceeds to capture) when live TCC reports granted, and
  `preflightUxRecording` continues to no-op when granted. Granting via B's
  Open-Settings path and relaunching does **not** leave A's explainer armed.
- **G14.3 — Consistent, truthful wording.** The two dialogs, where both can still
  appear, describe the same permission consistently and neither makes a claim the
  other contradicts about what is captured (still image vs. screen video vs. camera).
- **G14.4 — Default build unchanged.** With `TRAILER_UX_RECORDER` undefined, the
  screenshot explainer behaves **byte-for-byte** as in #59: shown lazily on first
  Take Screenshot / Acquire-from-Screenshot, gated by `screen_capture_explainer`,
  with no launch gate and no TCC dependency (Mechanism B is compiled out, so there
  is no live-TCC signal for A to consult on this path).

## Decision (proposed)

**Adopt Option B.** For recorder builds, **Mechanism B's launch-time,
relaunch-aware TCC gate is the primary and authoritative Screen-Recording surface**,
and Mechanism A's screenshot explainer defers to it:

1. **B stays authoritative in recorder builds** because it is the only one of the
   two that correctly handles the macOS trap that a Screen-Recording grant applies
   *only to the next launch* — it requests, deep-links, and quits so the relaunch
   picks up the grant. A lazy after-the-fact explainer cannot solve that, so B, not
   A, owns the launch-time decision.
2. **Share suppression state so neither re-prompts.** Have
   `maybeShowScreenCaptureExplainer` early-return (proceed straight to capture) when
   `uxScreenRecordingGranted()` already reports granted, and have both flows consult
   the **same** "already explained/granted" signal so that once Screen Recording is
   granted *or* the explainer acknowledged via **either** path, the other does not
   re-prompt. This closes both the double-prompt (G14.1) and the split-suppression
   (G14.2) failures.
3. **Unify the wording** so both dialogs read consistently about what is and isn't
   captured (still image vs. screen frames vs. camera), removing the privacy mixed
   signal (G14.3).

This decision is **proposed**; the implementation lands in this same PR
(`feat/ux-recorder`) for review, and the record moves to *accepted* once that
implementation meets G14.1–G14.4. Until then the Arbiter verdict below is
intentionally empty.

## Consequences

- **Recorder builds get a single, coherent Screen-Recording permission UX:** one
  launch-time authoritative prompt that survives the relaunch quirk, no second prompt
  on first screenshot once granted/acknowledged, and one consistent description of
  what is captured.
- **Default / non-recorder builds are unchanged:** Mechanism B is compiled out under
  `TRAILER_UX_RECORDER`, so #59's behaviour is preserved exactly (G14.4). The
  screenshot explainer still appears in non-recorder builds — where there is no
  launch gate and no TCC signal — as it does today.
- **A small coupling is introduced** between `ScreenCapturePermission` and the
  recorder's live-TCC check, present only when `TRAILER_UX_RECORDER` is defined; the
  non-recorder compile keeps A standalone with no new dependency.

## Alternatives considered

- **Option A — keep both flows independent.** Rejected: it is precisely the
  double-prompt and split-suppression defect this record exists to close.
- **Option C — drop #59's explainer in recorder builds only.** Viable fallback if
  sharing suppression state proves awkward; kept as the recorded fallback to B. Less
  preferred because it loses the screenshot-specific "still image, not video"
  reassurance in recorder builds where TCC isn't yet granted.
- **Option D — fully merge A and B into one permission class.** Deferred: the
  cleanest end state but a larger refactor that would touch #59's shipped
  non-recorder path, which G14.4 forbids this change from disturbing.

## Arbiter verdict + rationale

**Accepted.** The `feat/ux-recorder` implementation of Option B meets all four
thresholds this record set. G14.2 holds: `shouldShowScreenCaptureExplainer`
early-returns on the recorder's live-TCC probe and the granted `preflightUxRecording`
branch burns the shared `screen_capture_explainer` flag, so suppression is shared
across both flows and persists into a later non-recorder build (pinned by
`grantedPreflightSuppressesAndPersistsExplainer`). G14.3 holds: the two dialogs are
re-worded to one truthful voice about still-image-vs-video capture. G14.4 holds: the
non-recorder path is byte-for-byte #59 — the entire coupling is compiled out under
`TRAILER_UX_RECORDER` (verified: the branch's shipped-source delta is purely
additive, zero deletions). B stays authoritative because it alone survives the
macOS "grant applies to the next launch" trap; A defers rather than re-asking, so
no user is double-prompted or asked about a permission already granted. Sound,
honest, and non-regressing to #59 — accepted at arbiter level.

## Evidence required to reopen

Once accepted, reopen only on a concrete, checkable problem plus owner sign-off:
a real recorder-build flow where sharing suppression state still double-prompts or
mis-suppresses (e.g. a TCC state transition the shared signal reads wrong), a real
default-build regression of #59's explainer traced to this change, or an owner
decision to prioritise the full-merge end state (Option D) over the compile-gated
coupling. Naming "one dialog would be tidier" is not sufficient.
