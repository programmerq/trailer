#!/usr/bin/env python3
"""Build the nightly update-feed PAYLOAD json (before signing).

Usage: build-nightly-appcast-payload.py <output-file>

Reads TAG, BUILD_NUMBER, PUBLISHED_AT, MACOS_URL, MACOS_SHA from the
environment (set by .github/workflows/nightly.yml's "Build + sign nightly
update feed" step) and writes the payload object described in
src/update/UpdateFeedParser.h to <output-file>. Kept as a standalone script
(no heredoc in the workflow YAML) so it's independently readable, testable,
and diffable — mirrors scripts/compare-uat-baseline.sh and
scripts/parse-ctest-uat-summary.sh's shape.

This writes the PAYLOAD only — signing it into the final
{"payload":...,"signature":...} envelope is scripts/sign-update-feed.sh's
job, run separately (it needs the private key, which this script never
touches).
"""
import json
import os
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <output-file>", file=sys.stderr)
        return 2

    required = ["TAG", "BUILD_NUMBER", "PUBLISHED_AT", "MACOS_URL", "MACOS_SHA"]
    missing = [k for k in required if not os.environ.get(k)]
    if missing:
        print(f"build-nightly-appcast-payload.py: missing required env var(s): {', '.join(missing)}",
              file=sys.stderr)
        return 1

    payload = {
        "channel": "nightly",
        "entries": [{
            "tag": os.environ["TAG"],
            "build_number": int(os.environ["BUILD_NUMBER"]),
            "published_at": os.environ["PUBLISHED_AT"],
            "notes": (
                "Automated nightly build of Trailer's main branch. "
                "The update CHANNEL is ed25519-signed; the app bundle "
                "itself is unsigned/un-notarized (no Apple Developer "
                "Program enrollment) -- see PHILOSOPHY.md."
            ),
            "assets": {
                "macos": {
                    "url": os.environ["MACOS_URL"],
                    "sha256": os.environ["MACOS_SHA"],
                },
            },
        }],
    }

    with open(sys.argv[1], "w", encoding="utf-8") as f:
        json.dump(payload, f)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
