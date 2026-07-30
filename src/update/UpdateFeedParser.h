#pragma once

#include "Ed25519.h"
#include "UpdateTypes.h"

#include <QByteArray>
#include <QString>

#include <array>

namespace trailer::Update {

// Parses and cryptographically verifies a signed appcast feed. No
// networking here — UpdateChecker.cpp fetches the bytes; this file only
// ever sees bytes it has already been handed.
//
// On-wire format (see docs/decision-records/2026-07-30-nightly-auto-
// update-channel.md and scripts/sign-update-feed.py, the CI-side
// generator):
//
//   {
//     "payload": "<the exact UTF-8 text below, JSON-escaped as a string>",
//     "signature": "<base64, 64-byte ed25519 signature over the UTF-8
//                    bytes of that exact payload string>"
//   }
//
// `payload` decodes to:
//
//   {
//     "channel": "nightly",
//     "entries": [
//       {
//         "tag": "nightly-20260730",
//         "build_number": 4821,
//         "published_at": "2026-07-30T10:15:00Z",
//         "notes": "…",
//         "assets": {
//           "macos":   {"url": "…", "sha256": "…"},
//           "windows": {"url": "…", "sha256": "…"},
//           "linux":   {"url": "…", "sha256": "…"}
//         }
//       }, …
//     ]
//   }
//
// The signature covers the payload STRING bytes exactly as they sit
// inside the outer JSON (post-unescaping, i.e. what QJsonValue::toString
// returns) — never a re-serialization of the parsed object, which would
// make verification depend on this parser's own JSON formatting choices
// matching the generator's. Picking the string-inside-a-string shape
// sidesteps JSON canonicalization entirely.
struct ParsedFeed {
    bool ok = false;
    FeedEntry entry;
    // Populated only when ok is false — human-readable, safe to show in
    // the Preferences "Updates" pane / Check-for-Updates dialog. Never
    // includes raw untrusted feed content verbatim (avoids surprising a
    // user with attacker-controlled text in a system dialog).
    QString error;
};

// Verifies the outer signature against `publicKey` BEFORE parsing a
// single field out of the payload — an unverified feed is not trusted
// enough to read, let alone act on. Only after verification succeeds is
// the payload JSON parsed and the newest entry (highest build_number)
// extracted. Returns ok=false with `error` set for: malformed outer
// JSON, missing payload/signature fields, a signature that doesn't
// verify (tampered payload OR wrong key), malformed payload JSON, or a
// payload with zero entries.
ParsedFeed parseAndVerifyFeed(const QByteArray &rawFeedBytes,
                              const std::array<unsigned char, Ed25519::kPublicKeyBytes> &publicKey);

} // namespace trailer::Update
