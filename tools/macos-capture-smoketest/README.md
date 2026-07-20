# macOS capture-permission smoke test

A local, one-command harness that offsets the **manual GPSC.2 checklist** in
[PR #72](https://github.com/programmerq/trailer/pull/72) for Trailer's
ScreenCaptureKit picker capture backend.

GPSC.2 asks you to confirm, from a fresh TCC state, that switching
`capture_backend = screencapturekit` and taking a capture produces:

- **(a)** NO macOS "Screen Recording" permission prompt, and
- **(b)** NO entry under **System Settings ▸ Privacy & Security ▸ Screen &
  System Audio Recording** for Trailer.

This harness automates the reset and the grant assertion around a single
picker capture so you only have to do the click.

> **macOS-only.** ScreenCaptureKit and the TCC database are Apple-only, so this
> cannot run on Linux/CI. Like `MacScreenCapture.mm`, the owner's on-device run
> is the real validation — the script itself has only been shellcheck-verified
> off-device.

## Run it

```sh
# Manual drive (default, most reliable): the script resets + asserts,
# you do the one click when prompted.
tools/macos-capture-smoketest/smoketest.sh

# Launch a specific build for the [DRIVE] step:
tools/macos-capture-smoketest/smoketest.sh --app /Applications/Trailer.app --mode window

# Replay a recorded picker interaction (optional, needs cliclick):
tools/macos-capture-smoketest/smoketest.sh --app /Applications/Trailer.app --clicks picker.cliclick
```

Options: `--bundle-id ID` (default `io.github.programmerq.trailer`),
`--app PATH`, `--clicks FILE`, `--mode window|display` (informational),
`-h/--help`.

## Full Disk Access requirement

The authoritative per-app check reads the **system** TCC database at
`/Library/Application Support/com.apple.TCC/TCC.db`, which is SIP-protected.
The process running the script needs **Full Disk Access**:

1. **System Settings ▸ Privacy & Security ▸ Full Disk Access**
2. Enable your terminal (Terminal.app / iTerm) — or whatever app launches the
   script. If it's already listed, toggle it off and on.
3. **Fully quit and reopen** that terminal, then re-run.

Without FDA the script cannot read the system TCC.db. It prints these
instructions, marks the TCC-based checks **SKIP (needs Full Disk Access)**, and
degrades to the preflight diagnostic plus your own visual confirmation that no
prompt appeared. It also queries the per-user db
(`~/Library/Application Support/com.apple.TCC/TCC.db`) as a secondary source.

## What each step asserts

| Tag | Step | Asserts |
| --- | --- | --- |
| `[SETUP]` | `tccutil reset ScreenCapture <bundle-id>` | Clean TCC baseline. PASS if exit 0. |
| `[BASELINE]` | Query TCC.db pre-capture | No authorized `kTCCServiceScreenCapture` row for the bundle id yet. |
| `[DRIVE]` | Launch app / replay clicks / prompt | Triggers exactly one picker capture. |
| `[ASSERT]` | Re-query TCC.db post-capture | **GPSC.2 core:** still NO authorized grant → the picker consent left no standing grant. FAIL if a grant appeared. |
| `[DIAGNOSTIC]` | `CGPreflightScreenCaptureAccess()` | Harness process's own state — a sanity signal, not Trailer's grant. |

Final line is `RESULT: PASS / FAIL / INCONCLUSIVE`; the script exits non-zero
only on **FAIL**. `INCONCLUSIVE` means TCC checks were skipped (no FDA) and the
run needs your visual confirmation.

### `auth_value` meaning

On modern macOS the `access` table's authorization column is `auth_value`:

- `0` = denied, `2` = allowed, `3` = limited. `2`/`3` count as an authorized
  grant; `0` or an absent row does not.

Older schemas used an `allowed` column (`0`/`1`). The script detects which
column exists via `PRAGMA table_info(access)` and interprets it accordingly.

## Honest limitations

1. **Preflight measures the harness, not Trailer.** `CGPreflightScreenCaptureAccess()`
   reports the *calling* process's Screen-Recording state. It's a sanity signal
   (expected `not-granted` on a clean run), **not** proof about Trailer. The
   authoritative per-app answer is the `[ASSERT]` TCC.db query.
2. **Driving the system picker is hard.** The picker is system-drawn, so
   reliable automation is fragile. The default is therefore a **manual click**
   with the script handling reset + assertions around it. `cliclick` replay
   (`--clicks`) is optional and best-effort.
3. **No FDA → degraded mode.** If Full Disk Access can't be granted, the
   harness falls back to the preflight diagnostic plus **your own visual
   confirmation** that no permission prompt appeared and no entry was added
   under Screen & System Audio Recording.

## Deferred: CI integration

Running this unattended in CI needs a **persistent Mac runner** or an
auto-login VM matrix (e.g. `tart`) across the supported OS versions —
macOS **14.0**, **≥14.4**, and **15.x** — with a **Developer-ID-signed** build so
TCC attributes grants to a stable identity. That requires hardware/infra and is
**out of scope here**; this harness is the local, human-in-the-loop step in the
meantime.

## Cross-reference

Maps to the "Before this can flip the default" / GPSC.2 checklist in
[PR #72](https://github.com/programmerq/trailer/pull/72). The `[DRIVE]` + your
eyes cover GPSC.2 (a); `[ASSERT]` covers GPSC.2 (b).
