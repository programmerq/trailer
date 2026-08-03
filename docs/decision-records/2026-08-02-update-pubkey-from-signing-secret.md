# Update-channel public key comes from the signing secret, not a committed literal

- **Status:** accepted
- **Arbiter:** the update-channel agent role named for this decision; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-08-02
- **Date accepted / superseded:** 2026-08-02

## Context

The nightly auto-update channel
([`2026-07-30-nightly-auto-update-channel.md`](2026-07-30-nightly-auto-update-channel.md))
verifies a signed appcast against an ed25519 public key **embedded in the
binary** — deliberately, so a compromised feed host cannot nominate its own
key. What shipped embedded that key as a committed literal in
`src/update/UpdatePublicKey.h`, with a comment block instructing the owner to
replace it before release.

**What ships today, plainly:** that literal is a *throwaway development key*
whose private half was generated with a non-cryptographic PRNG and exists
nowhere. The owner has since provisioned a real keypair and set the
`TRAILER_UPDATE_SIGNING_KEY` secret, so `nightly.yml` now signs the feed with
the **real** key while every binary — including tonight's — still trusts the
**throwaway** one. Every shipped build therefore rejects every real signature.
Nothing is red: the signing step succeeds, the feed publishes, CI is green, and
the failure is visible only to a user who clicks *Check for Updates…*.

That is the decision on the table: not "which key", but **where the key a build
trusts comes from**, such that it cannot silently disagree with the key the feed
is signed with.

Relevant: `src/update/UpdateChecker.cpp:132` (the verify call site),
`scripts/sign-update-feed.sh` (the signer), AGENTS.md **G3** (no lying
controls) for what a keyless build may show the user.

## Options

- **A. Keep the committed literal; paste the real key in by hand.** One commit,
  no build machinery. The key and the secret are two independent sources of
  truth that must be manually kept in agreement, forever, including across
  every future rotation.
- **B. Commit the public key, and have CI assert it matches the secret.** Still
  a literal, but drift becomes loud instead of silent. Builds stay
  reproducible and secret-free. Requires a manual paste on first provision and
  on every rotation, with a red build until the paste lands.
- **C. Derive the public key from the signing secret at build time.** CI
  derives it once, early, and passes it to each build lane as a CMake
  variable; no key is committed. One input, so the trusted key and the signing
  key cannot disagree. Builds without the secret (local, PR, fork) get no
  update channel at all.
- **D. Fetch the key at runtime.** Rejected before reaching the personas: it
  defeats the entire point of embedding, per the 2026-07-30 record.

## Personas debate

- **Office non-technical user:** Has no view on key provenance, and a strong
  view on the symptom. Under A-as-shipped they click *Check for Updates…*, wait,
  and get an error mentioning a signature — indistinguishable from "the app is
  broken". They cannot tell a stale key from a hostile one, and would not know
  the difference matters. What they need is either an update that works or a
  control that plainly says it can't.
- **Older careful user:** The one with the most at stake, and the only persona
  with an opinion on the *unprovisioned* case. Reads "could not verify" as a
  security warning and will stop trusting the app rather than retry. Would
  rather be told up front "this build doesn't do updates" than be invited to
  click something that fails — and would be actively alarmed by a build that
  silently accepted an unverified update instead.
- **Power migrator:** Builds from source, so is exactly the person option C
  leaves without an update channel. Considers this correct and expected — they
  update by rebuilding — provided the UI says so rather than failing
  mysteriously. Would object to a source build silently trusting a key from the
  project's CI, and objects more strongly to one trusting a *throwaway* key,
  which is what A ships.
- **Occasional user:** Opens Trailer rarely, so is the most likely to be far
  behind and the most likely to meet a broken update path at exactly the moment
  it matters. Has no stake in which option is chosen, only in the failure being
  self-explaining when it happens.

## Admissible objections

- **The shipped state silently disables updates for every user** — any persona,
  *Help ▸ Check for Updates…*, gets a verification error caused by a key
  mismatch that no test, gate, or CI job detects. Raised against option A as it
  actually shipped; this is the defect prompting the record.
- **A hand-maintained pair drifts again at the next rotation** — Older careful
  user, a future key rotation, silently returns to exactly today's failure with
  no signal. Raised against A; option B answers it with a check, C by removing
  the second source.
- **A keyless build must not invite a click it cannot honour** — Office
  non-technical user, *Help ▸ Check for Updates…* in any local/PR/fork build
  under option C, would hit a guaranteed failure. This is G3, and it is the
  cost option C introduces; it is answered by disabling the control with a
  tooltip, not by leaving it live.
- **A keyless build must not fall back to *some other* key** — Power migrator,
  building from source, would otherwise ship a binary trusting a placeholder
  whose private half's whereabouts are unknown. Rules out "default to the
  committed dev key" as C's fallback.

### Rejected as naked preference

- "A generated header is harder to read than a committed constant." — rejected:
  states no concrete user, step, or failure. The generated file is written to
  the build tree and is readable there; no user flow depends on reading it.
- "Deriving in CI is over-engineering for one 32-byte value." — rejected: names
  no user or failure, and the value's size is not the risk; the silent-drift
  failure above is, and it has already occurred once.

## Checkable threshold this record would establish

1. No ed25519 public key literal exists in the repository's source.
2. A build configured **with** `TRAILER_UPDATE_PUBKEY` embeds exactly the key
   derived from the signing secret — verifiable by
   `scripts/derive-update-pubkey.sh <key.pem>` matching the bytes in the
   generated `UpdatePublicKey.h`.
3. A build configured **without** it reports
   `UpdateManager::isChannelProvisioned() == false`, issues **no** network
   request from `checkNow()`, and disables both update affordances (Help menu
   item; Preferences ▸ Updates button) with a tooltip stating why.
4. A feed produced by `scripts/sign-update-feed.sh` verifies against the key
   produced by `scripts/derive-update-pubkey.sh` from the same private key,
   asserted by an automated test that runs with no secret
   (`tests/test_update_pubkey.cpp`).
5. The nightly publish job refuses to publish a feed whose signing key does not
   match the key that night's binaries embed.

## Arbiter verdict + rationale

**Option C**, with the keyless state defined as "update channel compiled out"
rather than any fallback key.

The deciding objection is the second one: the failure being fixed is not a
wrong value, it is a *pair of values that can disagree without anything
noticing*. Option A is what produced it; fixing the literal by hand restores
the same structure and schedules a recurrence at the next rotation. Option B
genuinely answers that objection and was the closest call — it preserves
secret-free, reproducible builds and makes drift loud. It was rejected because
it still requires a human to transcribe a 64-character string on every
rotation, and because the check it adds fires *after* the mismatch exists,
whereas C makes the mismatch unrepresentable: there is one input.

C's real cost is the fourth objection's mirror image — local, PR, and fork
builds lose the update channel entirely. The personas accept this (the Power
migrator expects it; the others never build from source) *provided* the UI is
honest about it, which is why threshold 3 is part of this record rather than a
follow-up. The alternative fallback — keep trusting the committed dev key when
no secret is present — was rejected outright under the fourth objection: it
ships binaries trusting a key whose private half was generated with a
non-cryptographic PRNG, which is strictly worse than having no channel.

Secret exposure is the one place C is not free: deriving the public key
requires the private key, so `nightly-date` now reads the signing secret in
addition to `nightly-publish`. This is accepted as the minimum: the derivation
is a single step whose only output is public, the private key is piped into
`openssl` without touching disk, and the alternative (giving the secret to all
three build lanes so each can derive its own) is strictly worse. Deriving in
one early job and fanning out the *public* result is the narrowest arrangement
that works.

## Evidence required to reopen

- A nightly whose published feed fails to verify in a nightly binary from the
  same run — i.e. threshold 5's guard passing while the end-to-end path is
  still broken, which would mean the single-input premise is false.
- A demonstrated need for a source build to auto-update (a concrete user and
  flow, not a preference), which option C forecloses by design.
- A key-rotation procedure that requires two keys to be trusted simultaneously
  — the 2026-07-30 record's own "new pubkey signed by the old key" suggestion —
  which this record's single-key generation does not currently express, plus
  owner sign-off.
