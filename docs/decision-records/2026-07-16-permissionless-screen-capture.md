# Permissionless still-capture via the ScreenCaptureKit system picker

- **Status:** proposed
- **Arbiter:** macOS platform-integration lead (owner `programmerq` is the escalation-only override)
- **Date proposed:** 2026-07-16
- **Date accepted / superseded:** —
- **Builds on (companion, near-term):** the tactical Screen-Recording permission decision for the *current* `screencapture` capture path. Its source of record today is `docs/backlog/2026-07-13-macos-screenrecording-services-clarity.md`; a companion decision record is being authored for it under the new date+slug scheme. This record is robust to that companion's naming and does not require it to be present on `main`.
- **Separate from:** the UX-recorder permission story (PR #69's grandfathered ADR 0014).

## Context

Trailer's still-image capture shells out to the OS utility `/usr/sbin/screencapture`. "Acquire from Screenshot" (`Application::acquireFromScreenshot()`, `src/app/Application.cpp:367`) invokes it as `-i -x` (interactive selection, silent). "Take Screenshot" (`MainWindow::onTakeScreenshot()`, `src/ui/MainWindow.cpp:2285`) always passes `-x`, then per mode: Region → `-i -s`, Window → `-iW`, Entire Screen → no interactive flag (a non-interactive whole-screen grab). On the first real capture in any mode that reads back pixels, this trips the macOS **"Screen Recording"** TCC prompt. During v0.3.0 real-Mac dogfooding the owner saw the prompt and asked: doesn't macOS have a utility we can invoke that doesn't require us to hold this permission?

We already invoke that utility. The prompt fires anyway because **TCC attributes the screen-capture responsibility to the calling app (Trailer), not to `screencapture`** — the system tool carries no private standing grant that launders capture for arbitrary callers. So "shell out to the OS tool" does not, by itself, avoid the grant.

On-device behavior (owner's Retina Mac, fresh TCC reset, 2026-07-16): invoking capture (in the interactive Acquire / Region / Window modes) immediately shows the OS crosshair selection UI with **no** TCC prompt; the Screen Recording prompt fires only when the user click-drags a selection (the actual pixel readback). After **Deny**, subsequent captures still show the full selection UI (including space-bar mode cycling) but silently yield nothing — no re-prompt, even after relaunch — until the permission is manually reset. So the current path's selection UI is permission-free; TCC gates only the pixel readback, and **a denial is indistinguishable from a user-cancel** to Trailer (both return exit 0 with no file). A separate session is fixing that dead-end for the current path; this record is about the strategic backend choice.

macOS offers a sanctioned path where **the user's selection is the consent**: `SCContentSharingPicker` (a system-drawn picker) → `SCContentFilter` → `SCScreenshotManager.captureImage(...)` for a one-shot image. Because the pick happens inside a trusted system process, the model is designed so that no Screen Recording authorization dialog is presented and access is scoped to the picked content (Apple's framing; whether shipping releases are actually prompt-free is what GPSC.2 verifies on-device) — a portal-style, per-selection grant rather than a standing "record your whole screen forever" grant. This also sidesteps the macOS 15 (Sequoia) recurring re-authorization nag, which by construction targets apps that hold the standing grant ("…requesting to bypass the system private window picker and directly access your screen…").

### External grounding

- `SCContentSharingPicker`, `SCContentSharingPickerConfiguration`, `SCScreenshotManager`, and `SCScreenshotManager.captureImage(contentFilter:configuration:completionHandler:)` are all introduced at **macOS 14.0** per Apple's developer documentation (doc-data `introducedAt: 14.0`). Nothing in this chain requires 14.2/14.4/15.0. — developer.apple.com/documentation/screencapturekit
- WWDC23 session 10136 "What's new in ScreenCaptureKit" introduces `SCContentSharingPicker` as the system content picker that mediates between app and OS and hands back an `SCContentFilter` usable by both `SCStream` (continuous) and `SCScreenshotManager` (one-shot). — developer.apple.com/videos/play/wwdc2023/10136/
- The permission distinction (picker selection == consent, no standing grant needed) is stated by third-party engineering analysis contrasting the picker with the programmatic `SCShareableContent` path, which "requires the user to enable Screen Recording authorization in the System Settings app." — nonstrict.eu/blog/2023/a-look-at-screencapturekit-on-macos-sonoma/
- macOS 15 corroborates from the other side: the Sequoia prompt text frames "use the picker" vs. "bypass the picker for a standing grant" as the two modes, and the recurring re-auth nag hits standing-grant apps. — 9to5mac.com/2024/08/14/macos-sequoia-screen-recording-prompt-monthly/ ; mjtsai.com/blog/2024/08/08/sequoia-screen-recording-prompts-and-the-persistent-content-capture-entitlement/
- Verification caveat: in the *first* Sonoma beta the picker path erroneously still demanded authorization (filed FB12331920). The prompt-free behavior of a picker-derived filter driving `SCScreenshotManager` on shipping 14.x/15.x must be smoke-tested before we rely on it. — nonstrict.eu (same)
- The `screencapture -i` subprocess is **not** a clean permissionless path: TCC attributes to the caller (confirmed on-device above), and a sandboxed build cannot spawn interactive `screencapture` at all (`deny mach-register com.apple.screencapture.interactive`); the only workaround is a fragile temporary-exception entitlement with App-Review risk. Trailer is **not** currently sandboxed (Hardened-Runtime stub only, ad-hoc-signed non-notarized DMG, `platform/macos/entitlements.plist`), so this is a forward-looking constraint, not a present blocker — but it caps the subprocess path's future.
- `com.apple.developer.persistent-content-capture` (exempts a standing-grant app from the Sequoia nag) is restricted to VNC/remote-desktop-class apps and is not grantable to Trailer. `NSSharingService` is an export/share-sheet mechanism, not a capture source. Continuity Camera is a separate feature, out of scope.

### Deployment-floor reality (load-bearing)

`SCContentSharingPicker` + `SCScreenshotManager` require **macOS 14.0**. The coordinator reports `MACOSX_DEPLOYMENT_TARGET` is being aligned to 14.0 (the real ONNX-dylib floor), but `main` today (`95619cf`) still declares **11.0** (`scripts/build-macos.sh:57`, `docs/packaging-macos.md`, `Info.plist.in` `LSMinimumSystemVersion`). This record therefore gates the picker backend behind `@available(macOS 14.0, *)` and retains the current `screencapture` path as the macOS 11–13 fallback, independent of whether the 14.0 floor lands. Reliability caveat: while these APIs are *available* at 14.0, third-party testing rates the prompt-free picker behavior as only moderately reliable before **14.4** (on top of the first-Sonoma-beta FB12331920 regression). If the GPSC.2 smoke test shows the prompt-free behavior differs on 14.0–14.3, raise the picker floor to `@available(macOS 14.4, *)` and let Option A cover 11.0–14.3.

## Options

- **Option A — Status quo.** Keep `/usr/sbin/screencapture -i`; own the standing Screen Recording grant. Prompts once, pays the Sequoia recurring nag, and (today) dead-ends indistinguishably on deny. Works on macOS 11+.
- **Option B — RECOMMENDED. ScreenCaptureKit system picker.** `SCContentSharingPicker` → `SCContentFilter` → `SCScreenshotManager.captureImage(...)`. In-process API (not a subprocess), one-shot, sandbox-compatible, portal-style per-selection consent, no first-capture prompt, no Sequoia nag. Requires macOS 14.0; gated behind `@available(macOS 14.0, *)` with Option A as the pre-14 and edge-case fallback.
- **Option C — Non-interactive `screencapture`.** Drop `-i` — which is already what "Take Screenshot" ▸ Entire Screen does today. Rejected as a general path: it captures the whole screen with no user selection and still requires the standing grant attributed to the caller — strictly worse than A for the selection-based flows.
- **Option D — CoreGraphics `CGWindowListCreateImage` / `CGDisplayCreateImage`.** What `DESIGN.md:862` aspirationally specifies (but the shipped code never used). Rejected: deprecated in macOS 14, and still requires the standing Screen Recording grant.

## Personas debate

Unranked adversarial lenses. Only objections that name a persona, a step in a real flow, and the failure that user would hit are admissible.

- **Office non-technical user.** Doesn't know macOS calls a still screenshot "Screen Recording." Under A, a scanner-class app throwing a "wants to record your screen" prompt — then a monthly Sequoia re-prompt — reads as malware-ish and erodes trust. Under B they pick a window in the same system picker they already know from Zoom/Teams screen-sharing; no scary permission dialog. **Favors B.**
- **Older careful user.** Denies the Screen Recording prompt out of caution, then "Take Screenshot" silently does nothing, forever, with no explanation — a dead end they cannot self-diagnose. Under B there is no permission to deny; dismissing the picker is an unmistakable cancel they initiated. **Favors B — structurally removes the dead end.**
- **Power migrator (ex-Preview/Acrobat).** On Sequoia, the standing grant triggers Sequoia's recurring re-auth nag (weekly at 15.0, relaxed toward monthly / less often for frequently-used apps by 15.1); they expect a document tool not to behave like a screen recorder. B avoids the nag. **But** they capture often and want speed — a picker on every capture is friction vs. crosshair-immediately. **Mostly favors B; raises the friction objection below.**
- **Occasional user.** Uses capture rarely; under A the recurring re-prompt always catches them cold mid-task. B's stateless per-use consent never surprises them. **Favors B.**

## Admissible objections

- **Picker friction (Power migrator).** The picker adds a step and changes muscle memory versus the immediate crosshair. Admissible. Mitigation: within a session a returned `SCContentFilter` can be retained and re-driven through `captureImage` without re-presenting the picker (`allowsChangingSelectedContent` governs re-picking); confidence MODERATE — verify the filter stays valid across captures on-device. The picker is standard system UI users already know from screen-sharing.
- **Pre-14 users lose the feature (Older careful / Office user on macOS 11–13).** When they invoke Take Screenshot or Acquire from Screenshot on macOS 11–13, the picker API is unavailable and no capture is possible. Admissible. Mitigation: retain Option A as the pre-14 fallback behind `@available(macOS 14.0, *)`; no user loses capture.
- **FB12331920 regression risk (Older careful user hits a surprise prompt).** Admissible correctness concern. Mitigation: gate adoption on the GPSC.2 on-device smoke test; keep A as the fallback if the picker path ever prompts.

### Rejected as naked preference

- "ScreenCaptureKit is newer/cleaner, so we should use it." Taste, not a user failure — the user-facing justifications above stand on their own.
- "Shelling out to `screencapture` is hacky." Aesthetic. The real, admissible defects are TCC attribution to the caller and the deny dead-end, not hackiness.

## Checkable threshold this record would establish

- **GPSC.1** On macOS 14.0+, a user-initiated still capture completes with **no** "Screen Recording" TCC prompt and **no** entry created under System Settings ▸ Privacy & Security ▸ Screen & System Audio Recording, verified from a fresh TCC state (`tccutil reset ScreenCapture <bundleid>`).
- **GPSC.2** A picker-derived `SCContentFilter` drives `SCScreenshotManager.captureImage` **prompt-free** on the oldest supported 14.x (14.0), a mid-cycle 14.x (≥ 14.4), and a clean 15.x machine (guards against the FB12331920-class regression and the 14.0–14.3 reliability risk). This is the one load-bearing on-device check gating adoption.
- **GPSC.3** On macOS < 14, capture falls back to the existing `screencapture -i` path with no behavioral regression versus today.
- **GPSC.4** On the picker path, a capture outcome is unambiguous: either the user picked a source and an image was produced, or the user dismissed the picker (an explicit cancel). There is no silent deny-equals-cancel dead end.

## Arbiter verdict + rationale

Adopt **Option B** as the primary still-capture backend on macOS 14.0+, gated behind a capture-backend selector, with **Option A** (`screencapture -i`) retained as the macOS 11–13 and edge-case fallback. Rationale: B is the only option that meets the owner's actual goal — user-initiated capture with **no standing Screen Recording grant** — and it simultaneously removes the deny dead-end (GPSC.4) and the Sequoia nag. A is not dropped; it becomes the compatibility fallback, which also de-risks the FB12331920 caveat.

**Do not implement in this docs-only PR.** The recommendation is unambiguous, but the implementation is a new Objective-C++ capture backend (`SCContentSharingPicker` delegate + `SCScreenshotManager`) whose correctness depends on an on-device smoke test (GPSC.2) we cannot run in CI — it does not meet the "trivial enough to prototype inline" bar. Per the project's no-hollow-PRs norm, implementation should follow once this decision is **accepted** (owner-gated) and GPSC.2 passes, landing behind a `capture_backend` flag (`sck-picker` | `screencapture`) so the fallback stays one switch away.

**Knock-on — the pre-permission explainer.** `src/platform/ScreenCapturePermission.{h,cpp}` exists solely to pre-warn the user before the `screencapture` path trips the "Screen Recording" TCC prompt. On the picker backend there is no such prompt — the picker itself is the consent surface — so **for stills on macOS 14+ the explainer becomes unnecessary and should be retired, not merely reworded.** This intersects the owner's separate "explainer is too verbose" copy work: on the picker path the right answer for stills is to remove the dialog, not shorten it. The explainer stays for the `screencapture` fallback (macOS 11–13), and the UX-recorder keeps its own gate (see below).

**Reconciliation with ADR 0014.** Still-capture (this record) and the UX-recorder (0014) are **separate permission stories**. The recorder captures continuously and legitimately needs sustained screen access; 0014's single-prompt/recorder-gate decision is unchanged by this record. Whether the recorder also adopts the picker + `SCStream` to shed its own standing grant is 0014's call, noted here as a cross-reference, not decided here.

## Evidence required to reopen

- The GPSC.2 smoke test shows the picker path **does** prompt for Screen Recording or creates a standing grant on shipping 14.x/15.x (FB12331920-style regression) → keep Option A, reopen.
- The deployment floor stays < 14.0 indefinitely, making Option B unreachable for the majority of users → reconsider prioritization.
- UX testing shows the per-capture picker friction is unacceptable and no viable in-session filter reuse exists → reconsider or add an opt-in "remember source" affordance.
