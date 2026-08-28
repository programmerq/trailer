---
id: 2026-08-04-macos-nightly-openssl-skip-confirm
title: Confirm on macOS that test_update_pubkey skips (or runs) instead of failing on LibreSSL
priority: TBD
status: open
source: nightly-20260804 run 30907188776 (macOS job 91985006187) + the fix that followed
created: 2026-08-04
---

## Threshold

One **macOS** nightly completes its gating unit-test step with
`test_update_pubkey` reporting either

- `SKIP : TestUpdatePubkey::opensslSignedFeedVerifiesAgainstDerivedKey()` with a
  message naming the missing ed25519 capability, **or**
- `PASS` on that slot (which is what happens if the runner's Homebrew has
  `openssl@3` installed),

and in both cases `ctest -LE uat` exits 0 on macOS arm64. A `FAIL` on that
slot, or a skip whose message does not name why, means this item is **not**
closed.

Note which of the two outcomes occurred when closing: a PASS means the
production-signer interop check is live on macOS as well as Linux; a SKIP
means the mac runner has no `openssl@3` and the check remains Linux-only,
which is acceptable (nightly signs the feed on the Linux publish runner) but
worth recording.

## Context

The 2026-08-04 macOS nightly failed its gating unit-test step with
`test_update_pubkey`:

```
FAIL!  : TestUpdatePubkey::opensslSignedFeedVerifiesAgainstDerivedKey()
         (openssl exited 1: Algorithm ed25519 not found
          usage: genpkey [-algorithm alg] ...)
```

macOS ships **LibreSSL** as `/usr/bin/openssl`, whose `genpkey` has no
ed25519. The slot's guard was `openssl version` — which LibreSSL answers — so
it checked existence, not capability, and the test failed where it should have
skipped. It was the test's first-ever macOS execution (PR #143 merged after
the 08-03 nightly). No DMG and no signed appcast were produced.

The fix (`scripts/find-openssl-ed25519.sh`, wired into
`tests/test_update_pubkey.cpp` and `scripts/test-derive-update-pubkey.sh`)
probes the three ed25519 operations the shipped scripts actually use, prefers
Homebrew's keg-only `openssl@3` when `brew --prefix` reports one, and skips
with a concrete reason otherwise. The LibreSSL refusal is reproduced on Linux
by a stub in `scripts/test-find-openssl-ed25519.sh`, so the *rejection* is
proven without a Mac — but which branch the real mac runner takes (skip vs.
Homebrew-openssl pass) is unobserved, and only a macOS run shows it.

## Related

- `2026-08-03-macos-nightly-ocr-window-segv-confirm` — the other macOS nightly
  blocker. `test_ocr_window` passed on 2026-08-04 (night 1 of the 3 that item
  requires); this test was the *only* remaining red in that lane's gating step.
- `2026-07-26-macos-uat-triage` — the standing macOS-specific triage item.
