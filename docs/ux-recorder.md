# UX Session Recorder (developer-only)

A compile-time-gated, strictly local recorder for capturing detailed
usability sessions of Trailer on the maintainer's own machine: what was
done in Trailer, what appeared on screen, the camera's view of the
user, raw input, and the moments where Trailer was abandoned for
Preview.

**Open work:** known gaps and the active backlog (from reviewing real
recording sessions) live in [ux-recorder-todo.md](ux-recorder-todo.md).

**Philosophy compliance.** PHILOSOPHY.md bans telemetry. This feature
is not telemetry: it is OFF at compile time by default, records only in
builds explicitly compiled with the recorder enabled (and even then a
single launch can opt out with `--no-ux-record`), writes only to the
local disk, and contains **no network code of any kind** — no HTTP
clients, no upload paths, no analytics services. Code review should
reject any change that introduces an outbound-capable class into
`src/uxrecord/`.

---

## Building with the recorder

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTRAILER_ENABLE_UX_RECORDER=ON
cmake --build build --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
```

`TRAILER_ENABLE_UX_RECORDER` defaults to **OFF**. Default builds:

- do not compile any of `src/uxrecord/` except the no-op facade header,
- do not link AVFoundation / ScreenCaptureKit / ImageIO,
- do not declare `NSCameraUsageDescription` in Info.plist,
- do not register the `--ux-record` / `--no-ux-record` CLI options (they
  are rejected as unknown options),
- never trigger any recording permission prompt.

Default builds never record. The whole feature is gated so the shipped
app is byte-for-byte unaffected.

Non-macOS recorder builds compile against a stub capture backend
(`StubUxPlatformCapture.cpp`): the Qt event stream, semantic events,
markers, and the session report all work; screen/camera/global-input
record themselves as unavailable in `events.jsonl`. Windows/Linux
backends can later replace the stub behind the same
`UxPlatformCapture` interface without touching Trailer-side logic.

## Starting a session

**Recorder-enabled builds record every launch by default.** This is so
the recorder build can be set as the default macOS handler for PDFs and
images (Finder → Get Info → Open With → Change All…) and still capture
sessions opened straight from Finder, which pass no CLI arguments. Just
launch Trailer — or double-click a file it owns — and recording starts.

```sh
# macOS (bundle build) — these are all equivalent to "just launch it":
open build/Trailer.app
./build/Trailer.app/Contents/MacOS/Trailer [files...]
./build/Trailer.app/Contents/MacOS/Trailer --ux-record [files...]  # explicit, redundant
```

To skip recording for a single launch (e.g. debugging unrelated app
behaviour), pass `--no-ux-record`:

```sh
./build/Trailer.app/Contents/MacOS/Trailer --no-ux-record [files...]
```

> Rationale: the original design required an explicit `--ux-record` per
> run. That was changed to record-by-default because this is a private,
> sole-developer build whose entire purpose is to harvest sessions from
> normal day-to-day use as the default file opener; an opt-out
> (`--no-ux-record`) is the convenient inverse. `--ux-record` is still
> accepted so old habits and the acceptance walkthrough below keep
> working. If you ever want "build the feature but don't auto-record",
> the place to reintroduce that split is `main.cpp` (the `cli.uxNoRecord`
> check) — the recorder core does not care how it was started.

While recording, every window shows a red **● REC** chip in the status
bar and a **◉ Recording** menu in the menu bar:

| Action | Shortcut | Effect |
|---|---|---|
| Insert Frustration Marker | ⌘⇧M | `manual_marker` kind `frustration` + window screenshot |
| Insert Unexpected-Behavior Marker | — | kind `unexpected_behavior` |
| Insert Important-Moment Marker | — | kind `important_moment` |
| Insert Note Marker… | — | kind `note` with free text |
| Hand Off to Preview | ⌘⇧Y | see below (macOS only) |
| Pause Screen & Input Capture | — | master pause for frames + global input (camera keeps rolling) |
| Show Session Folder | — | opens the session directory in Finder |

⌘⇧M also works **while Preview is frontmost** (observed by the global
event tap, when Input Monitoring is granted), so a frustration moment
in the fallback app can be flagged without switching back.

The session stops cleanly at quit (`aboutToQuit`). Events flush every
2 s; manual markers flush immediately.

### Hand Off to Preview

`◉ Recording → Hand Off to Preview` is the instrumented "I'm giving up
and doing this in Preview" gesture:

1. saves the document if dirty (so Preview sees current state),
2. records `preview_fallback_started` with Trailer state
   (`document_kind`, `page`, `zoom`, `view_mode`, `active_tool`,
   `reason: "explicit_preview_fallback"`),
3. inserts a `preview_handoff` marker (+ screenshot),
4. opens the file via `/usr/bin/open -b com.apple.Preview`,
5. capture follows the app switch automatically (`app_activated`
   events; screen frames and global input continue while Preview is
   frontmost, pause when anything else is).

The action is disabled (with an explanatory tooltip) until the current
document has a file on disk. Trailer does not attempt to control
Preview's page/zoom.

## macOS permissions

Prompts appear only in recorder builds, the first time each capture
starts. All are optional — denying one records an event and the rest of
the session continues.

| Permission | Used for | When missing |
|---|---|---|
| Screen Recording | ScreenCaptureKit display frames | startup gate (below); degraded `screen`; no frames this session |
| Camera | AVFoundation face-cam movie | `camera_permission_denied`; degraded `camera`; no camera file |
| Input Monitoring | global event tap (input while Preview is frontmost) | `input_tap_unavailable`; degraded `input`; Trailer-local input still recorded via Qt |

### First-run Screen Recording is a two-launch dance (expected)

This is the one rough edge worth internalising. macOS applies a Screen
Recording grant **only to future launches of a binary, never the
process that asked**. So starting a session without it would capture
everything *except* the screen. To stop that from silently wasting a
recording, the first launch without the grant shows a **blocking gate**
(`Application::preflightUxRecording`) before recording begins:

- **Open Settings & Quit** *(default)* — registers Trailer in the
  Screen Recording privacy list, deep-links you straight to the toggle,
  and quits. Approve it there, then relaunch (with record-by-default,
  "relaunch" is just opening another file Trailer owns). From then on
  every session captures the screen — the grant persists, so you do
  this **once per machine, ever**.
- **Record Without Screen** — proceed now with a degraded session
  (input + camera record; `screen/` stays empty). The session is marked
  degraded (see below).
- **Don't Record This Launch** — run Trailer normally with no session.

If you'd rather front-load everything, approve Screen Recording (and
Camera / Input Monitoring) once in System Settings → Privacy & Security
before your first session and the gate never appears.

The same future-launch rule applies to **Input Monitoring** (the global
event tap that records input while Preview is frontmost), but it is not
gated — a missing input grant only loses *Preview*-frontmost input
(Trailer's own input is fully captured Qt-side regardless), so it is
handled by the degraded-stream marking rather than a blocking dialog.
Camera, by contrast, takes effect immediately on Allow.

### Degraded sessions are marked, not silent

Whenever a stream fails (screen / camera / input), the recorder:

- adds it to a `degraded` array in `manifest.json` — rewritten live, so
  even a crashed session shows what was missing, and a downstream
  consumer never mistakes an empty `screen/` for a healthy session;
- turns the **● REC** chip amber and relabels it (e.g. `● REC · no
  screen`) with the reason in its tooltip, for the whole session — not
  just the transient status-bar flash;
- records the underlying event (`screen_recording_permission_pending`,
  `camera_permission_denied`, `input_tap_unavailable`, …) in
  `events.jsonl`.

`ux-session-summary.py` echoes the `degraded` list. Note that a missing
**Input Monitoring** preflight does *not* by itself mark input degraded:
the listen-only tap frequently still delivers pointer/scroll/modifier
events even when the OS reports the permission as not-granted, withholding
only key content — so `0` `source:"macos"` `key` events is not the same
as "no input" (all Trailer keystrokes are captured Qt-side). Only a tap
that fails to start at all (`input_tap_unavailable`) marks input degraded.

Screen capture additionally requires macOS 12.3+ (ScreenCaptureKit);
older systems record `screen_capture_unsupported` and continue.
ScreenCaptureKit is weak-linked so recorder builds still launch on
macOS 11 (the project's deployment target).

## Session directory format

Sessions live under the per-user app-data location — on macOS:

```
~/Library/Application Support/Trailer/ux-sessions/
  2026-06-09T21-14-33Z-1a2b3c4d/        # UTC start + short session id
    metadata.json     # app/qt/os versions, pid, CLI args, screens, config
    manifest.json     # status: recording | complete | crashed; counts
    events.jsonl      # the timeline (schema below)
    trailer.log       # qDebug/qWarning tee for the session
    screen/           # frame-000123-001234567.jpg  (seq + elapsed_ms)
    camera/           # camera-000.mov (one continuous 640×480 segment)
    screenshots/      # marker-000042-frustration.png (window grabs)
```

(`XDG_DATA_HOME/trailer/ux-sessions` on Linux, `AppData` on Windows —
same `AppPaths::dataDir()` root as every other Trailer artefact.)

Everything is written incrementally: `metadata.json` and a
`status: "recording"` manifest at start, JSONL/log flushed every 2 s,
frames and camera written by their own subsystems as they happen. A
crash therefore loses at most a couple of seconds of events. The next
`--ux-record` launch sweeps sessions whose manifest still says
`recording` with a dead pid and rewrites them as `status: "crashed"`.

## Event schema (`events.jsonl`)

One JSON object per line. Shared envelope:

```json
{
  "schema_version": 1,
  "session_id": "1a2b3c4d",
  "sequence": 123,
  "timestamp_utc": "2026-06-09T21:14:51.512Z",
  "elapsed_ms": 18432,
  "source": "trailer",
  "type": "document_opened",
  "data": {}
}
```

- `sequence` — strictly increasing per session; total order even when
  wall clocks misbehave.
- `elapsed_ms` — monotonic (QElapsedTimer) milliseconds since session
  start. **This is the canonical alignment axis** for every stream:
  screen frame filenames embed it, `screen_frame` / `camera_started` /
  marker events carry it in the envelope.
- `source` — who observed it:
  - `session` — lifecycle (`session_started`, `session_stopped`)
  - `trailer` — semantic app events (below)
  - `qt` — application-level Qt observation (input, focus, dialogs,
    menus) while Trailer is active
  - `macos` — platform capture (frontmost apps, frames, camera,
    permissions, global input)
  - `log` — `log_message` tee of qDebug/qWarning/qCritical

Semantic `trailer` types currently emitted: `document_opened`,
`document_closed`, `document_focused`, `document_state_changed`
(page/zoom/view-mode/dirty transitions, polled at 500 ms, with a
`changed` field list + full `state` snapshot), `action_triggered`
(every menu/toolbar QAction, with `state_before`), `tool_selected`,
`form_tool_selected`, `operation_failed` / `operation_succeeded` /
`status_message` (the status-bar flash chokepoints),
`preview_fallback_started`, `manual_marker`, `marker_screenshot`,
`window_attached`, `visual_capture_paused` / `_resumed`.

`qt` input types: `mouse_button`, `mouse_path` (moves sampled at
≥30 ms and batched — never one disk record per move), `wheel`
(accumulated per flush), `key` (with printable `text`), `shortcut`,
`focus_changed`, `window_activated`, `dialog_opened/closed`,
`menu_opened/closed`, `window_shown/hidden`, `application_state`.

`macos` types: `platform_capture_started/stopped`, `app_activated`
(bundle id, name, pid, kind ∈ trailer/preview/other),
`screen_capture_started/failed/unsupported/stopped`,
`screen_recording_permission`, `screen_frame` (file + frontmost),
`camera_permission_requested/denied`, `camera_unavailable`,
`camera_started/stopped`, `input_monitoring_permission`,
`input_tap_started/unavailable`, plus tap-observed `mouse_button`,
`mouse_path`, `wheel`, `key`, `modifiers_changed` (each tagged with
`frontmost`).

**Keystroke capture is intentionally on** (private developer tool).
The single switch to keep only key identity and drop printable text is
`kCaptureKeyText` in `src/uxrecord/UxRecorder.cpp`; it governs both
the Qt-side and the macOS-tap capture.

## Timestamp synchronization

All streams share one clock: the session's monotonic `elapsed_ms`.

- events: `elapsed_ms` in the envelope;
- screen frames: `screen/frame-<seq>-<elapsed_ms>.jpg` **and** a
  `screen_frame` event per frame;
- camera: the `camera_started` event's `elapsed_ms` is the offset of
  the .mov's t=0 within the session (delegate-confirmed start);
- marker screenshots: tied by `marker_sequence` to their
  `manual_marker` event.

`timestamp_utc` is for humans and cross-referencing external logs; use
`elapsed_ms`/`sequence` for ordering.

## Capture behaviour and limitations

- **Screen = main display, frame-gated.** The whole main display is
  streamed; frames are *retained* only while Trailer or Preview is
  frontmost (and not paused). Overlapping third-party windows visible
  during those periods are captured — acceptable for a private
  recorder. Secondary displays are not captured.
- **Frames, not video.** ~3 fps JPEG (`kScreenFps`,
  `kScreenJpegQuality`, `kScreenMaxLongSidePx` in
  `MacUxPlatformCapture.mm`). Budget ≈ 0.5–1 MB/s while active. Fine
  motion is reconstructed from `mouse_path` events; assemble video
  with ffmpeg when needed (below).
- **Camera keeps rolling** across app switches and pauses — it records
  the user's reactions, not the screen. One continuous
  `camera/camera-000.mov`; no audio (out of scope by design).
- **Global input is recorded only while Preview is frontmost** (UXR-004).
  While Trailer is frontmost its input is recorded Qt-side with widget
  context, so the global tap deliberately drops Trailer-frontmost input
  rather than emit a redundant second `source:"macos"` stream; while any
  unrelated app is frontmost nothing is recorded. Net: `source:"qt"`
  input ≈ Trailer, `source:"macos"` input ≈ Preview, no overlap.
- **Qt-side action instrumentation** covers actions existing at window
  construction; dynamically rebuilt menus (Open Recent) report through
  `document_opened` rather than `action_triggered`.
- **IDocument state is polled** (500 ms) — page-turn timestamps are
  accurate to ±0.5 s; the input events around them are exact.
- A session that outlives `pid_max` recycling could theoretically be
  mis-marked crashed/alive; ignored as a non-problem for a dev tool.

## Analyzing a session

Validate / summarize (stdlib-only Python):

```sh
python3 scripts/ux-session-summary.py "<session-dir>"
```

`jq` sketches:

```sh
# Frustration timeline with what was on screen state-wise
jq -c 'select(.type=="manual_marker")' events.jsonl

# What happened in the 10 s before the first frustration marker
T=$(jq -s 'map(select(.type=="manual_marker"))[0].elapsed_ms' events.jsonl)
jq -c --argjson t "$T" 'select(.elapsed_ms >= ($t-10000) and .elapsed_ms <= $t)' events.jsonl

# Time spent per frontmost app
jq -s 'map(select(.type=="app_activated")) | .[] | {at:.elapsed_ms, app:.data.kind}' events.jsonl

# All failed operations with the page/tool state active at the time
jq -c 'select(.type=="operation_failed")' events.jsonl
```

Screen frames → video for scrubbing:

```sh
cd screen && ffmpeg -framerate 3 -pattern_type glob -i 'frame-*.jpg' -c:v libx264 -pix_fmt yuv420p ../screen.mp4
```

SQLite/DuckDB import:

```sql
-- duckdb
SELECT type, count(*) FROM read_json_auto('events.jsonl') GROUP BY type ORDER BY 2 DESC;
```

### Reasonable next steps for report processing

Deliberately **not** implemented yet:

- a CLI that slices `events.jsonl` around markers and drafts a
  Markdown issue (events + nearest screen frames + camera still);
- a timeline viewer (events.jsonl + frames are enough for a simple
  HTML scrubber);
- OpenCV/vision-model passes over `screen/` keyed by `screen_frame`
  events; local LLM summarisation of the semantic stream;
- Whisper transcription if audio is ever added (requires adding an
  audio input to the camera session + `NSMicrophoneUsageDescription`);
- Windows/Linux `UxPlatformCapture` backends.

## Manual test plan (macOS)

The capture half cannot run headless; after touching it, verify the
acceptance flow by hand: build with the option ON, launch Trailer (no
flag needed — recorder builds record by default), confirm the REC chip,
open a PDF, scroll/zoom/annotate, ⌘⇧M, Hand Off to Preview, interact
there, ⌘⇧M again in Preview, return to Trailer, quit. Then check the
session directory: manifest `complete`, `events.jsonl` parses
(`ux-session-summary.py`), frames cover both apps but not e.g. a Finder
interlude, `camera-000.mov` plays, and `rg -c "http" events.jsonl` finds
nothing surprising.

On the very first run on a fresh machine, expect `screen/` to be empty
and the REC chip to be amber (Screen Recording not yet effective — see
the two-launch dance above); approve it, relaunch, and re-verify that
`screen/` fills. Pass `--no-ux-record` once to confirm a recorder build
can still launch as plain Trailer with no session created.

## Architecture map

```
src/uxrecord/
  UxRecord.h               always-compiled facade; no-ops when disabled
  UxRecord.cpp             facade impl (recorder builds)
  UxRecorder.{h,cpp}       session lifecycle, dirs, manifest/metadata,
                           log tee, flush timer, markers
  UxEventStream.{h,cpp}    JSONL envelope/sequencing/batching writer
  UxQtEventCapture.{h,cpp} app-wide Qt observer (coalesced input, focus,
                           dialogs/menus)
  UxTrailerHooks.cpp       attachToMainWindow(): REC chip, Recording
                           menu, action/tool/document instrumentation,
                           Preview hand-off
  UxPlatformCapture.h      narrow platform interface + factory
  MacUxPlatformCapture.mm  ScreenCaptureKit + AVFoundation + NSWorkspace
                           + CGEventTap backend
  StubUxPlatformCapture.cpp non-macOS placeholder backend
```

Application owns the recorder (`Application::startUxRecording()`, called
from `main.cpp` on every recorder-build launch unless `--no-ux-record`);
MainWindow's only touches are facade calls (`uxrecord::recordEvent` in
the flash/addDocument chokepoints and `uxrecord::attachToMainWindow(this)`
at the end of the constructor), so default builds are bit-identical in
behaviour.
