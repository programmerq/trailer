# Screen-capture permission preflight: check TCC before the OS selection UI

- **Status:** accepted
- **Slug:** `capture-permission-preflight`
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-16
- **Date accepted / superseded:** 2026-07-16 (accepted)
- **Extends:** ADR-0014 (recorder Screen-Recording reconciliation, PR #69) — a
  grandfathered numbered record, **not yet on `main`** (still on the
  `feat/ux-recorder` branch, so a relative file link would not resolve here).
  0014 keeps the recorder's launch-time TCC gate (*Mechanism B*) as the
  **authoritative** Screen-Recording surface in recorder builds and has the
  screenshot explainer (*Mechanism A*) **defer to live TCC** (proceed straight
  to capture when granted). This record is the NOW behaviour for **still
  capture**: it makes the two `screencapture` sites preflight live TCC directly.
  That is the same live-TCC deference 0014's G14.2 mandates — Granted → Proceed
  — so the two reconcile rather than conflict; **the recorder launch gate is
  untouched** (still capture never consults or fires Mechanism B).
- **References:** backlog note
  [`2026-07-13-macos-screenrecording-services-clarity`](../backlog/2026-07-13-macos-screenrecording-services-clarity.md)
  §(a) — whose *Resolution note (2026-07-15)* shipped the first-run explainer +
  a post-hoc "no image captured" hint but **never checked live TCC state**. This
  record corrects that implementation guidance (adds the preflight); the note's
  keep-vs-suppress Services ruling §(b) is untouched.

> **Numbering note.** Per the latest owner ruling, decision records now use
> `docs/decision-records/YYYY-MM-DD-<slug>.md`. This record is the first under
> that scheme; earlier records (ADR-0001…0016) keep their grandfathered numbers.
> An earlier draft of this same record was filed as `0016-…` and used a sticky
> persisted marker to split Denied from Undetermined — that design is **not**
> what shipped; see "What an earlier draft got wrong" below.

## Context

Trailer imports screenshots via two user-triggered actions that shell out to
`/usr/sbin/screencapture` on macOS:

- **Tools ▸ Take Screenshot** — `MainWindow::onTakeScreenshot()`
  (`src/ui/MainWindow.cpp`), which has a status bar and used `flashStatus`.
- **File ▸ Acquire from Screenshot** — `Application::acquireFromScreenshot()`
  (`src/app/Application.cpp`), which has **no** window/status bar and used a
  modal `QMessageBox`.

**What shipped before this record** (so it is not misread as the target): each
site called `maybeShowScreenCaptureExplainer(...)`, gated only on the persisted
`screen_capture_explainer` flag, then ran `screencapture`, then on
`exitCode() != 0` showed "Screen capture cancelled." and on exit-0-empty-file
showed "No image was captured…". **No code path ever queried the live Screen
Recording (TCC) permission state.**

An owner dogfood pass on real macOS caught three concrete defects:

1. **No preflight.** After a TCC reset (e.g. an OS upgrade) the explainer flag
   is still burned, so the app goes straight to the OS crosshair. After an
   explicit *Deny* it keeps launching the crosshair into a void — capture
   silently yields nothing. Denial is indistinguishable from a user cancel.
2. **Deny is a silent permanent dead-end.** The flow never degrades and never
   re-informs; the feature is simply broken with no route to recovery.
3. **The "Screen capture cancelled" popup/flash is pure noise.** Cancelling the
   OS selection is a no-op the user just performed; narrating it back violates
   PHILOSOPHY (*a popup is a last resort*; no dialog that narrates the user's
   own action or a no-op).

macOS labels this permission "Screen Recording" even for a still screenshot and
supplies no app-facing usage-description string, so the app must reason about
the permission itself.

## What an earlier draft got wrong (the BLOCKER)

An earlier draft of this record persisted a `screen_capture_attempted` marker
and used it to split the not-granted case: attempted-before → **Denied**,
never-attempted → **Undetermined**. A reviewer proved this manufactures a false
Denied and **dead-ends worse than before**:

> After `tccutil reset ScreenCapture` following a prior grant,
> `CGPreflightScreenCaptureAccess()` returns false but the sticky marker is
> still set → the code computes **Denied** → it degrades **without ever
> re-requesting**. Because it never spawns `screencapture` and never calls
> `CGRequestScreenCaptureAccess()`, the app is not even registered in the
> Screen Recording pane it deep-links to — a worse dead-end than the original
> bug.

The correction: **never manufacture a Denied state from a sticky bool.**
`CGPreflightScreenCaptureAccess()` can only authoritatively report *Granted*.
Every not-granted case is reported as *Undetermined* and routed through
`CGRequestScreenCaptureAccess()`, which **prompts the OS when truly
undetermined** (recovering the post-reset case and re-registering the app in
TCC) and is a **silent no-op returning false when actually denied** (so no
crosshair appears). The `screen_capture_attempted` marker is removed entirely.

## Options

- **(A) Keep the flag-only gate, add richer post-hoc messaging.** Cheapest, but
  cannot fix defect 1 (still launches the crosshair on a denied/undetermined
  state) and cannot make denial distinguishable from cancel.
- **(B) Preflight the live TCC state before the OS UI, via an injectable,
  pure-decidable layer, routing every not-granted case through
  `CGRequestScreenCaptureAccess`.** Query the provider
  (`CGPreflightScreenCaptureAccess` on macOS), map (state, explainer-
  acknowledged) through a pure decision function to one of {Proceed,
  ShowExplainerFirst, RequestAccess, DegradeDenied}, and route each call site
  accordingly. Fully unit-testable off-Mac. **Adopted.**
- **(C) Full native 3-state API (`SCShareableContent`, macOS 14+).** The
  cleanest denied-vs-undetermined signal, but raises the minimum OS and adds a
  ScreenCaptureKit dependency; deferred as future work (tracked separately as
  the strategic permissionless-capture effort).

## Personas debate

- **Office non-technical user:** After an OS upgrade reset the crosshair appears
  with no context, or nothing happens at all. Needs the app to explain and,
  when blocked, to hand them a working route to fix it — not a dead crosshair.
- **Older careful user:** Denied on purpose once; expects that choosing *Deny*
  is respected and not repeatedly re-poked with an OS crosshair, and expects a
  plain-language path to change their mind later. `CGRequestScreenCaptureAccess`
  is a silent no-op once denied, so this does not re-poke — it simply degrades.
- **Power migrator:** Expects Esc to cancel silently — a cancelled capture is a
  no-op and must not raise a dialog or a status flash narrating it back.
- **Occasional user:** Won't know "Screen Recording" is the permission name for
  a still screenshot; when blocked, needs one message that names it and points
  at System Settings.

## Admissible objections

- **Any user, denied dead-end (defect 1+2):** after Deny, the current flow
  launches the OS crosshair into a void and the capture silently produces
  nothing, with no degrade and no recovery route — a broken feature with no
  visible cause. Fixed by DegradeDenied: no OS UI, a recoverable pointer to the
  setting.
- **Reviewer, sticky-marker false-Denied (the BLOCKER):** a persisted
  attempted-marker computes Denied after a post-grant reset and degrades
  without re-requesting, leaving the app unregistered in the very pane it links
  to. Fixed by removing the marker and routing every not-granted case through
  `CGRequestScreenCaptureAccess`.
- **Power migrator, noisy cancel (defect 3):** a "Screen capture cancelled"
  popup/flash narrates the user's own Esc — a visibility-of-system-status
  anti-pattern that trains users to dismiss dialogs. Fixed by making cancel
  fully silent.
- **Office user, post-reset crosshair (defect 1):** a burned explainer flag +
  reset TCC sends the user straight to a crosshair with no context. Fixed by
  preflighting: an Undetermined state routes back through the explainer, then
  the OS request re-prompts.

### Rejected as naked preference

- "Always show a confirmation toast after a successful capture." — rejected:
  the imported image *is* the confirmation; a toast narrates a success the user
  can already see.
- "Auto-open System Settings on every denied windowed attempt." — rejected as
  heavy-handed and inconsistent with the ask-first modal: the windowed path
  degrades with a status-bar flash whose text is the recovery route; it does not
  fling open a settings window unbidden.

## Decision

Before either `screencapture` site launches the OS selection UI, the flow
consults `queryScreenCapturePermissionState()` (no persisted state, a bare read
of live TCC) and routes through the pure `decideScreenCaptureFlow()` table:

- **Granted → Proceed** — spawn `/usr/sbin/screencapture` straight away. Only a
  Granted preflight ever spawns the capture tool.
- **Undetermined → RequestAccess** — call `CGRequestScreenCaptureAccess()`. It
  **prompts** when truly undetermined (re-registering the app, e.g. after
  `tccutil reset`) and is a **silent no-op returning false** when denied. On
  `true` → capture; on `false` → degrade. No crosshair ever appears on a denial.
- **Denied → RequestAccess** — the macOS provider never manufactures Denied from
  a sticky bool (the enumerator is kept for interface completeness and the
  decision-table unit tests), but it too routes through the request, which is a
  silent no-op → degrade with the honest message.

### Explainer retired for stills (owner decision 2026-07-17)

The stills capture flow **no longer shows a pre-permission explainer modal**; it
leans on the OS Screen Recording prompt directly (triggered via
`CGRequestScreenCaptureAccess` on first not-granted use), matching standard app
behaviour. The decision table therefore collapses from four actions to two —
`{Proceed, RequestAccess}` — and both not-granted states route to
`RequestAccess`; the `ShowExplainerFirst` / `DegradeDenied` actions are removed.

- **Rationale.** With the preflight + `CGRequestScreenCaptureAccess` rework the
  OS prompt already fires at exactly the right moment, so a preceding modal was
  a popup in front of a popup — the popup-as-last-resort friction PHILOSOPHY
  argues against. The burden of proof was on **keeping** the explainer, and
  "Screen Recording sounds scary for a still" is speculative. The only concrete
  gap the OS dialog leaves is that wording; the chosen mitigation is at most a
  single **passive status-bar line** — NOT a modal — deferred unless it proves
  confusing in practice (no speculative UI).
- **Aligns with PR #72 (permission-less capture)**, which already concluded the
  explainer is retired for stills once the ScreenCaptureKit picker lands; this
  record does it now for the shell-out path.
- **The explainer helper API is retained** (`shouldShowScreenCaptureExplainer`,
  `acknowledgeScreenCaptureExplainer`, `maybeShowScreenCaptureExplainer`) for
  the recorder path (#69's ADR-0014) to decide independently — only the stills
  call sites stopped invoking it.

Both call sites share the exact same decision table, the same message
(`screenRecordingNeededMessage`), and the same deep link
(`screenRecordingSettingsUrlString` / `openScreenRecordingSettings`).

- **User-cancel** (`exitCode() != 0`) is **silent** at both sites — no flash, no
  dialog.
- **Empty capture** (exit 0, zero-byte file) is **silent** at both sites — a
  Granted user who selected nothing. It does **not** assert a permission
  problem (we only reach the capture block after Granted or a successful
  request), correcting the earlier draft's false "Screen Recording is off"
  claim during a legitimate first-run grant.
- **Degrade copy is honest** — `screenRecordingNeededMessage()` reads
  *"Trailer needs Screen Recording permission to capture the screen. Enable it
  in System Settings ▸ Privacy & Security ▸ Screen Recording, then reopen
  Trailer."* — true whether the OS was just prompted or the permission is
  denied, and it names the relaunch nuance (a newly-granted permission may not
  take effect until Trailer is reopened).
- **Degrade surface:** status-bar `flashError` on the windowed path (no unbidden
  auto-open of System Settings — the flash text is the recovery route); one
  ask-first `QMessageBox` with an "Open System Settings" button on the no-window
  Acquire path.

The pure decision function, the settings-URL string, the needed message, and
the non-mac provider fallback are covered by `tests/test_screen_capture_flow.cpp`
and run on Linux CI.

## Arbiter verdict + rationale

**Accepted 2026-07-16.** Option **(B)** is adopted, in its corrected form: a
pure, injectable decision layer where every not-granted case routes through
`CGRequestScreenCaptureAccess` rather than being split by a persisted marker.
The admissible objections (denied dead-end, sticky-marker false-Denied, noisy
cancel, post-reset crosshair) each name a concrete actor, step, and failure and
are all resolved. Option (A) cannot fix defect 1; option (C) is the right
long-term shape but is deferred to avoid raising the minimum macOS version now.

Design commitments:

- **Injectable 3-state provider + pure decision function.** The state provider
  (`queryScreenCapturePermissionState`) and the request wrapper
  (`requestScreenCaptureAccess`) are the only platform-specific seams;
  `decideScreenCaptureFlow` is pure and fully unit-tested off-Mac.
- **Only Granted is authoritative; every not-granted case is arbitrated by the
  request.** `CGPreflightScreenCaptureAccess()` reflects live TCC without
  prompting, but cannot tell Denied from Undetermined — so the provider reports
  Undetermined and `CGRequestScreenCaptureAccess()` does the prompting (when
  undetermined) or the silent no-op (when denied). **No sticky marker.**
- **Cancel and empty-capture are silent** (upholds PHILOSOPHY: no dialog
  narrating the user's own action or a no-op).
- **Denial is recoverable**, never a silent permanent dead-end.

## Manual-verification checklist (real-Mac, owner)

TCC cannot be exercised in CI. The owner must verify on real macOS. Bundle id is
`org.trailer.Trailer` today; **PR #71** renames it to
`io.github.programmerq.trailer` — a bundle-id rename resets the TCC grant, so
re-verify after it lands. Recheck script (run the applicable id):

```sh
# today
tccutil reset ScreenCapture org.trailer.Trailer
# after PR #71's rename
tccutil reset ScreenCapture io.github.programmerq.trailer
```

1. **Undetermined path.** `tccutil reset ScreenCapture <bundle-id>`, then Take
   Screenshot (and, separately, Acquire from Screenshot) → expect the **OS
   permission prompt directly** (no pre-permission explainer step; the explainer
   is retired for stills). No crosshair before granting.
2. **Deny is recoverable, not a dead-end.** At the OS prompt choose *Deny* →
   expect a status-bar flash (windowed) / actionable modal (Acquire) degrade,
   **no crosshair**, and re-invoking Take Screenshot **re-prompts / re-degrades**
   cleanly (the request path is a no-op while denied but the flow never
   dead-ends).
3. **Reset-after-grant (the BLOCKER journey).** Grant once, capture successfully,
   then `tccutil reset ScreenCapture <bundle-id>` and Take Screenshot again →
   expect a **re-prompt**, not a false-Denied degrade. This is the exact journey
   the earlier sticky-marker draft broke.
4. **Relaunch nuance.** After granting at the OS prompt, note whether the same
   session can capture immediately or whether Trailer must be **reopened** for
   the new grant to take effect. The "then reopen Trailer" copy assumes a
   relaunch may be needed; confirm and adjust the copy if capture works in-session.
5. **Deep link target.** Confirm
   `x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture`
   opens the **Screen Recording** pane on current macOS. Note this is the
   **legacy anchor id** (`com.apple.preference.security` + `Privacy_ScreenCapture`);
   if a future macOS moves the anchor, update `screenRecordingSettingsUrlString()`.

## Evidence required to reopen

- A measured real-Mac case where routing every not-granted case through
  `CGRequestScreenCaptureAccess` misbehaves (e.g. a genuine deny that still
  shows a crosshair, or an undetermined state the request fails to prompt),
  plus owner sign-off — this would justify moving to the `SCShareableContent`
  3-state API (option C).
- **Bundle-id caveat:** the TCC grant is keyed to the app bundle id. Trailer is
  `org.trailer.Trailer` today; **PR #71** renames it to
  `io.github.programmerq.trailer`. Because a bundle-id change resets the TCC
  grant, the renamed build will read Undetermined on first run even for users
  who previously granted — the preflight handles this correctly (re-shows the
  explainer, lets the OS re-prompt), but it is called out here so the reset is
  not later mistaken for a regression.

## Consequences

- **Cancel and empty-capture are silent** — the "Screen capture cancelled." and
  false "no image / Screen Recording is off" flashes/popups are removed at both
  sites (taste rule: no dialogs narrating the user's own action/no-op, no false
  permission assertions during a legitimate grant).
- **Denial is recoverable** — both sites route the user to
  System Settings ▸ Privacy & Security ▸ Screen Recording with an honest,
  relaunch-aware message instead of dead-ending, and re-prompt on the next
  attempt.
- **No pre-permission explainer for stills** (owner decision 2026-07-17) — both
  stills sites lean on the OS Screen Recording prompt directly; the decision
  table is now `{Proceed, RequestAccess}` and the `capture-explainer.png`
  evidence shot is removed as misleading. The explainer helper API stays in
  place for the recorder path (#69) to decide separately. The only residual
  trade-off is the OS prompt's "Screen Recording" wording for a still; the
  reserved fix is a single passive status-bar line, not a modal, deferred unless
  it proves confusing.
- **No sticky marker** — the `screen_capture_attempted` key is gone; the post-
  reset case recovers via the OS request instead of computing a false Denied.
- **Non-mac is unaffected** — `queryScreenCapturePermissionState` returns
  `Granted` off-Mac (no TCC) and `requestScreenCaptureAccess` returns `true`, so
  the `QScreen::grabWindow` fallback path is unchanged and no CoreGraphics
  symbol is compiled on Linux/Windows (both calls are behind `#ifdef
  Q_OS_MACOS`; `CoreGraphics` is linked only inside `if(APPLE)`).
