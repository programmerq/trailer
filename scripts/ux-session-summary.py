#!/usr/bin/env python3
"""Validate and summarize a Trailer UX recording session directory.

Usage:
    python3 scripts/ux-session-summary.py <session-dir>

Stdlib only, read only, local only (docs/ux-recorder.md). Exits
non-zero when the session fails basic validation (unparseable JSONL,
missing required files, non-monotonic sequence numbers) so it can
double as a sanity check in scripts.
"""

import json
import sys
from collections import Counter
from pathlib import Path


def fail(message: str) -> None:
    print(f"INVALID: {message}", file=sys.stderr)
    sys.exit(1)


def main() -> None:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        sys.exit(2)
    session = Path(sys.argv[1])
    if not session.is_dir():
        fail(f"not a directory: {session}")

    for required in ("manifest.json", "metadata.json", "events.jsonl"):
        if not (session / required).exists():
            fail(f"missing {required}")

    manifest = json.loads((session / "manifest.json").read_text())
    metadata = json.loads((session / "metadata.json").read_text())

    events = []
    last_sequence = 0
    with (session / "events.jsonl").open() as stream:
        for line_number, line in enumerate(stream, 1):
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as error:
                fail(f"events.jsonl line {line_number}: {error}")
            sequence = event.get("sequence", 0)
            if sequence <= last_sequence:
                fail(
                    f"events.jsonl line {line_number}: sequence {sequence} "
                    f"not greater than previous {last_sequence}"
                )
            last_sequence = sequence
            events.append(event)

    if not events:
        fail("events.jsonl is empty")

    duration_ms = events[-1].get("elapsed_ms", 0)
    types = Counter(e.get("type", "?") for e in events)
    sources = Counter(e.get("source", "?") for e in events)
    markers = [e for e in events if e.get("type") == "manual_marker"]
    failures = [e for e in events if e.get("type") == "operation_failed"]
    app_switches = [e for e in events if e.get("type") == "app_activated"]

    screen_frames = sorted((session / "screen").glob("frame-*.jpg"))
    camera_files = sorted((session / "camera").glob("*"))
    screenshots = sorted((session / "screenshots").glob("*.png"))

    print(f"session   {manifest.get('session_id')}  [{manifest.get('status')}]")
    print(f"started   {manifest.get('started_utc')}  (app {metadata.get('app_version')}, "
          f"qt {metadata.get('qt_version')}, {metadata.get('os')})")
    print(f"duration  {duration_ms / 1000.0:.1f}s   events {len(events)}")
    print(f"sources   {dict(sources)}")
    print(f"artefacts {len(screen_frames)} screen frames, "
          f"{len(camera_files)} camera files, {len(screenshots)} marker screenshots")

    if app_switches:
        print("app focus timeline:")
        for event in app_switches:
            data = event.get("data", {})
            print(f"  {event.get('elapsed_ms', 0) / 1000.0:9.1f}s  "
                  f"{data.get('kind', '?'):8s}  {data.get('bundle_id') or data.get('name', '')}")

    if markers:
        print("markers:")
        for event in markers:
            data = event.get("data", {})
            note = f"  — {data['note']}" if data.get("note") else ""
            print(f"  {event.get('elapsed_ms', 0) / 1000.0:9.1f}s  "
                  f"{data.get('kind', '?')}{note}")

    if failures:
        print("failed operations:")
        for event in failures:
            print(f"  {event.get('elapsed_ms', 0) / 1000.0:9.1f}s  "
                  f"{event.get('data', {}).get('message', '')}")

    print("top event types:")
    for event_type, count in types.most_common(12):
        print(f"  {count:6d}  {event_type}")

    if manifest.get("status") == "recording":
        print("note: manifest still says 'recording' — crashed or in-flight session.")


if __name__ == "__main__":
    main()
