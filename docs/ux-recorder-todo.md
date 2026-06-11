# UX Session Recorder — TODO / known gaps

Backlog for the developer-only UX recorder (see [ux-recorder.md](ux-recorder.md)
for the design). Each item below is self-contained: a cold agent should be
able to pick one up from this file plus the cited `file:line` anchors without
needing the originating review conversation.

**Provenance.** These items came out of reviewing a real recording session
(`session_id be7c3105`, recorder build 0.2.0, macOS Sequoia 15.7.7, arm64,
ScreenCaptureKit path) in which the **screen stream produced zero frames while
the camera worked**. Root cause was not a capture bug — it was a first-run
permission/relaunch interaction (detailed in UXR-001). The session is on the
maintainer's machine and is **not** retained in-repo; everything needed to act
is captured here.

> Status legend: `open` (not started) · `in-progress` · `done`.
> Priority: **P1** (do first) · **P2** · **P3** (nice-to-have).
> None of these are being implemented in the session that wrote this file.

---

## UXR-001 — Gate the first session on relaunch-required permissions  ·  P1  ·  open

**Problem.** macOS grants Screen Recording (ScreenCaptureKit) and Input
Monitoring (IOKit HID tap) only to *future* launches of a process — granting
mid-session does **not** enable capture for the running process; the camera
(AVFoundation) is the opposite and applies its grant to the live process. On a
first `--ux-record` run with Screen Recording not yet granted, the current code
preflights, fires the system prompt, then proceeds into a session anyway — so
`SCShareableContent` returns `"The user declined TCCs for application, window,
display capture"` at ~50 ms and **no screen frames are ever written**, even
though the user may have clicked *Allow*. In the source session this silently
wasted a full 6-minute recording; the gap was only discovered by inspecting the
output directory afterward.

**Evidence (session be7c3105).** `screen_recording_permission {granted:false,
requesting:true}` at 10 ms → `screen_capture_failed "...user declined TCCs..."`
at 50 ms → `camera_started` at ~5 s (camera grant applied live). Final
artefacts: `0 screen frames, 1 camera file, 7 marker screenshots`; manifest
`status:"complete"`.

**Affected code.**
- [`MacUxPlatformCapture.mm:384`](../src/uxrecord/MacUxPlatformCapture.mm) `startScreenCapture()` — `CGPreflightScreenCaptureAccess()` / `CGRequestScreenCaptureAccess()` at lines 387–395, then unconditionally calls `getShareableContentWithCompletionHandler` at 400.
- [`MacUxPlatformCapture.mm:746`](../src/uxrecord/MacUxPlatformCapture.mm) `startInputTap()` — same relaunch semantics for `IOHIDCheckAccess` / `IOHIDRequestAccess` (747–754).

**Proposed direction.** On `--ux-record` launch, preflight the
relaunch-required grants *before* the recorder begins. If any are missing,
don't silently start a doomed session — present one blocking, actionable dialog
that lists what's missing and offers: **[Open Privacy Settings]** (deep-link
`x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture`
/ `…?Privacy_ListenEvent`) and **[Quit & Relaunch]** (and a **[Record anyway]**
escape hatch that proceeds with a clearly-degraded session). Camera, which
applies live, should keep its current in-session prompt and is out of scope for
this gate.

**Acceptance criteria.**
- First run on a machine with Screen Recording ungranted shows the gate instead
  of producing an all-zero-frame session.
- Choosing *Quit & Relaunch* (after granting) yields a session with screen
  frames on the next launch.
- *Record anyway* still works and the resulting session is marked degraded (see
  UXR-002).
- Default (non-recorder) builds are unaffected — gate lives entirely under
  `TRAILER_ENABLE_UX_RECORDER`.

---

## UXR-002 — Make degraded-stream state persistent and machine-visible  ·  P2  ·  open

**Problem.** When a stream fails, the only signal today is a single transient
status-bar flash (`captureIssue` → "UX recorder: <type> — session continues
without it."). It scrolls by in the first ~1.2 s and is gone. Worse, the
session manifest still reads `status:"complete"` with no indication a stream was
missing, so any downstream consumer (including a future analysis agent) sees a
"healthy" session and only learns the truth by scanning `events.jsonl` or
running `ux-session-summary.py`. The empty `screen/` directory is itself
ambiguous — it can't distinguish "permission denied" from "never frontmost."

**Affected code.**
- [`UxRecorder.cpp:208`](../src/uxrecord/UxRecorder.cpp) — the `context.emitEvent`
  chokepoint already classifies `denied`/`unavailable`/`failed` events (213–220).
  This is the natural place to accumulate a degraded-stream set.
- [`UxRecorder.cpp:430`](../src/uxrecord/UxRecorder.cpp) `writeManifest()` — the
  `status=="complete"` branch (444–447) is where a `degraded` / `warnings` field
  should be serialized.
- [`UxTrailerHooks.cpp:176`](../src/uxrecord/UxTrailerHooks.cpp) — the `● REC`
  status-bar chip (176–182); its `captureIssue` handler can also relabel/recolor.

**Proposed direction.**
1. Accumulate degraded streams (e.g. `"screen"`, `"camera"`, `"input"`) in the
   emitEvent classifier and write a `degraded: [...]` (and/or `warnings: [...]`)
   array into the `complete` manifest.
2. Promote the chip to a persistent warning state for the whole session when a
   stream is degraded — e.g. `● REC · no screen` in a warning color, with the
   reason in its tooltip — instead of relying on the momentary flash.
3. Optional: a post-session toast / Finder reveal that says e.g. "Session saved
   — screen capture did not run."

**Acceptance criteria.**
- A session where screen capture failed has `degraded` containing `"screen"` in
  `manifest.json`.
- The REC chip visibly reflects the degraded state for the session's duration,
  not just at the moment of failure.
- `ux-session-summary.py` continues to report the artefact counts (already does)
  and can optionally echo the manifest `degraded` list.

---

## UXR-003 — Accurate input-permission reporting (mouse-but-no-keystrokes)  ·  P2  ·  open

**Problem.** `IOHIDCheckAccess()` at startup is not a reliable predictor of what
the CGEventTap actually delivers, and the discrepancy is silently misreported.
In the source session the recorder logged `input_monitoring_permission
{granted:false}` and `input_tap_started`, yet the listen-only tap went on to
capture **109 `mouse_button` + 95 `mouse_path` + 51 `modifiers_changed` + 4
`wheel` events — and exactly 0 `key` events** (all 2,299 keystrokes came from the
Qt-side capture). So under a partial/again-needs-relaunch grant, the tap
delivered pointer + modifier-flag events but the system withheld keyDown/keyUp
content. A consumer reading the permission events would wrongly conclude global
input was entirely absent.

**Affected code.**
- [`MacUxPlatformCapture.mm:746`](../src/uxrecord/MacUxPlatformCapture.mm) `startInputTap()` — `IOHIDCheckAccess` reporting (747–755) vs. the tap created regardless (770–787).
- [`MacUxPlatformCapture.mm:878`](../src/uxrecord/MacUxPlatformCapture.mm) `handleTapEvent()` — where key vs. mouse events are emitted.

**Proposed direction.** Treat the permission events as advisory, and report
*observed* tap capability instead of (or in addition to) the preflight check:
e.g. after the tap has run briefly, emit a single event summarizing what it is
actually receiving (`mouse: yes, keyboard: no`), or detect the
"modifiers-changed but never keyDown" signature and surface it. At minimum,
document the keyboard-vs-pointer asymmetry under partial grant so analysis code
doesn't mistake "0 macos key events" for "no input."

**Acceptance criteria.**
- A run where the tap delivers mouse but not keystrokes produces an event (or
  manifest note) that distinguishes that state from "tap fully unavailable."
- The recorder no longer implies global input is absent when it is in fact
  partially capturing.

---

## UXR-004 — Stop the global tap from duplicating input while Trailer is frontmost  ·  P3  ·  open

**Problem.** The global input tap's purpose (per design) is to record input
*while Preview is frontmost*. But `handleTapEvent` retains input whenever the
frontmost app is **not** `Other` — which includes **Trailer**. While Trailer is
frontmost, the Qt-side capture already records the same clicks/moves *with widget
context*, so the tap produces a redundant second `source:"macos"` mouse stream.
In the source session (which never switched to Preview) this meant ~204
duplicate macos pointer events shadowing the richer Qt stream — extra disk and a
double-counting trap for any analysis that sums across sources. The duplication
is documented as intentional in ux-recorder.md, so this is a deliberate
trade-off to revisit, not a regression.

**Affected code.**
- [`MacUxPlatformCapture.mm:878`](../src/uxrecord/MacUxPlatformCapture.mm) `handleTapEvent()` — gate at 882–887 (`front == Other` → drop).

**Proposed direction.** Gate tap-sourced input on `front == Preview` only (keep
the frustration-hotkey handling, which is already Preview-gated at 935–939),
letting the Qt side own input while Trailer is frontmost. Update the
"two streams distinguished by source" note in ux-recorder.md accordingly.

**Acceptance criteria.**
- A Trailer-only session contains no `source:"macos"` `mouse_*` / `wheel` events.
- A session that hands off to Preview still records global input there.

---

## UXR-005 — Reduce the camera's storage footprint  ·  P3  ·  open

**Problem.** The face-cam, not the screen stream, dominates session size. In the
source session `camera/camera-000.mov` was ~151 MiB of a ~163 MB total (~97%) —
roughly 3.5 Mbps for 640×480 over ~6 minutes, which is heavy for a 480p
reactions cam (the screen frames the recorder is actually after budget at only
~0.5–1 MB/s while active).

**Affected code.**
- [`MacUxPlatformCapture.mm:676`](../src/uxrecord/MacUxPlatformCapture.mm) `cameraSessionBody()` — session preset `AVCaptureSessionPreset640x480` (693–695) and `AVCaptureMovieFileOutput` (696).

**Proposed direction.** Lower the camera bitrate (e.g. set
`AVVideoCompressionPropertiesKey` / `AVVideoAverageBitRateKey` on the output
connection, or drop the preset), and/or make the face-cam opt-in via a config
flag, given it is the largest artefact and the least central to UX analysis.

**Acceptance criteria.**
- A comparable-length session yields a materially smaller `camera-000.mov` with
  the face still readable, **or** the camera is cleanly skippable via config and
  records `camera` as intentionally-off rather than failed.
</content>
</invoke>
