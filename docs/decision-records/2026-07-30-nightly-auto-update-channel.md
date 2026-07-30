# Decision record: nightly channel, ed25519-signed, custom checker (not Sparkle)

<!--
This record uses the date+slug naming scheme (docs/decision-records/YYYY-MM-DD-<slug>.md).
Refer to it by slug/date, not a number. It follows TEMPLATE.md and the process in
PHILOSOPHY.md → "How design decisions get adjudicated".
-->

- **Status:** accepted <!-- proposed | accepted | superseded-by <slug> -->
- **Arbiter:** distribution/release-engineering (agent role; the owner, programmerq, is the escalation-only override)
- **Date proposed:** 2026-07-30
- **Date accepted / superseded:** 2026-07-30 — the owner answered all four forks raised in the Phase-1 research report directly, in-session: (1) custom ed25519 checker over Sparkle 2, (2) nightly-channel-only for this pass, (3) feed as a release asset (not GitHub Pages), (4) auto-check-but-manual-download/install. This record documents that decision and its implementation; see "PR body" (the accompanying PR) for the phase-1 research this settles.

## Context

ROADMAP.md's "Now" item 1 has stood as the top pickable item for several
release windows: "existing installs can discover and pull new releases over a
cryptographically signed channel that does **not** require Apple Developer
Program enrollment." PHILOSOPHY.md frames this as *deferred, not designed
out* — Trailer is not enrolled in the Apple Developer Program (the $99/yr
Developer-ID/notarization gate) and has no plan to be
(`TODO-packaging.md`'s indefinite deferral). Today the `.app` ships unsigned
and un-notarized; `README.md` documents the one-time
`xattr -dr com.apple.quarantine` Gatekeeper bypass users run by hand.

The owner's actual near-term ask (2026-07-30) is narrower than "build the
whole channel": they want to run the **latest nightly** on their dogfooding
Mac with minimum friction, not necessarily the eventual stable-release
channel. `.github/workflows/nightly.yml` already publishes unsigned,
partial-success, per-OS nightly builds as GitHub prereleases tagged
`nightly-YYYYMMDD`. Nothing before this record made those discoverable or
verifiable from inside the app — a user had to know to check GitHub by hand.

This record settles the four forks a Phase-1 research pass (this PR's
companion report) raised, all previously blocking implementation per the
owner's explicit instruction not to receive a single-file proposal doc
without questions asked and answered first.

## Options

**Fork 1 — library.** *(A)* Sparkle 2 (macOS) / WinSparkle (Windows): mature,
native progress UI, delta patches; new framework dependency, own update-UX
conventions to reconcile with Trailer's consent posture. *(B)* A thin custom
checker against GitHub's Releases API with our own ed25519 verification:
zero new runtime dependency, one small reviewed networking file (mirrors
`src/ml/ModelDownloader.cpp`'s shape), same code serves Windows/Linux later
with no second library. ROADMAP.md names both as qualifying under the
no-Apple-Dev policy; only Velopack is explicitly disqualified
(Developer-ID/Authenticode trust model).

**Fork 2 — channel scope.** *(A)* Nightly-channel-only this pass, with the
UI shaped (a channel selector) so Stable slots in later without a redesign.
*(B)* Build both channels' plumbing now, even though `release.yml` has no
signed-feed step yet.

**Fork 3 — feed hosting.** *(A)* Generate the signed feed as an additional
GitHub Release **asset** on the nightly release itself (the same mechanism
`uat-summary.json` already uses) — zero new publish step. *(B)* GitHub
Pages — a permanent, tag-independent URL, but a new deploy step nightly.yml
doesn't have today.

**Fork 4 — update aggressiveness.** *(A)* Silent background download +
install, prompting only to relaunch — lowest click-count, but writes to
`/Applications` (or the Windows/Linux equivalent) with no per-download user
action. *(B)* Auto-*check* only (once opted in); every download and install
step stays a deliberate, individually-clicked user action.

## Personas debate

- **Office non-technical user:** not the target of a dogfood nightly
  channel; largely unaffected either way. Would be confused by *any*
  unattended write to their Applications folder they didn't ask for in the
  moment — favors Fork 4 = B.
- **Older careful user:** the reference user's stand-in for "don't surprise
  me." Wants the update visible and reversible before it happens — favors
  Fork 4 = B (see the download/install step as its own click, not folded
  into "check"). Indifferent to Fork 1/3 (implementation detail); wants
  Fork 2 = A so the feature ships sooner rather than waiting on a
  twice-as-large first version.
- **Power migrator:** wants the fastest path to "running latest main" with
  the fewest clicks — the owner's literal stated goal. Would push for Fork
  4 = A (silent auto-install) but explicitly deferred to the safer default
  in this session; tension noted, not overridden.
- **Occasional user:** would forget this feature exists between checks;
  auto-check (opted in) matters more to them than manual-check ever will —
  supports keeping "Check for Updates…" always reachable (G3) independent
  of the toggle, which both Fork-4 options preserve.

## Admissible objections

- **Power migrator, "download an update," failure: an unattended write to
  `/Applications` that silently replaces a running app's bundle is exactly
  the surprise PHILOSOPHY's "no new outbound network calls without an
  explicit, off-by-default toggle" line was written to prevent generalizing
  from "checking" to "installing."** Raised against Fork 4 = A.
- **Any user, "the app auto-installs an update it can't finish cleanly,"
  failure: on macOS specifically, a failed mid-swap leaves the user with a
  half-replaced `.app` bundle and no running Trailer to tell them so.**
  Raised against Fork 4 = A regardless of persona — this is why even Fork 4
  = B's manual install step keeps the previous bundle until the new one is
  verified in place (see Implementation notes below).
- **Distribution-cost lens (not a named persona, but a real constraint):
  the maintainer runs this repo's CI/release pipeline personally; a second
  publish surface (GitHub Pages) is a second thing to keep working and
  explain when it breaks, for a feature whose only consumer right now is
  the owner's own dogfooding machine.** Raised against Fork 3 = B.

### Rejected as naked preference

- "Sparkle just feels more polished / native" — rejected: not tied to a
  concrete user/step/failure; the custom checker's disabled-state, tooltip,
  and consent framing are held to the same G2/G3 bar regardless of library.

## Checkable threshold this record would establish

- **Fork 1 (B, custom checker):** `QNetworkAccessManager` / `QNetworkReply`
  appear in exactly one new file, `src/update/UpdateChecker.cpp` — grep the
  diff for these types outside that file plus `src/ml/ModelDownloader.cpp`
  and their tests; zero hits passes.
- **Fork 2 (A, nightly-only):** the Preferences → Updates channel combo
  contains a "Stable" entry that is `QStandardItem::isEnabled() == false`
  with a non-empty tooltip explaining why and where the status lives
  (`src/ui/PreferencesDialog.cpp`); only "Nightly" performs a real check.
- **Fork 3 (A, release asset):** the signed feed is fetched via
  `browser_download_url` on a GitHub Release asset named
  `appcast-nightly.json`, discovered through the public Releases list API
  (`src/update/UpdateChecker.cpp`) — no `raw.githubusercontent.com` /
  GitHub Pages URL appears anywhere in the update path.
- **Fork 4 (B, manual download/install):** `UpdateManager::startDownload()`
  and `installAndRelaunch()` are each reachable only from an explicit
  `QPushButton::clicked` handler (`src/ui/PreferencesDialog.cpp`), never
  from `maybeAutoCheck()` or any signal internal to `UpdateChecker`/
  `UpdateManager` — grep confirms no call path from the auto-check timer
  to either method.

## Arbiter verdict + rationale

**Accept the owner's answers as given: B / A / A / B** (custom checker,
nightly-only, release-asset hosting, auto-check-but-manual-install).

- **B over Sparkle (Fork 1):** the admissible frugality objection (a new
  runtime dependency vs. a small reviewed file) and the fact that the same
  code generalizes to Windows/Linux without waiting on WinSparkle both
  favor B; the owner's own stated default matched the Phase-1
  recommendation. See the accompanying PR body for the further, explicit
  audit of whether an ed25519-capable crypto library is *already* linked
  into Trailer (checked, not assumed — it is not, on any of the three
  platforms; qpdf is deliberately built with no external crypto provider on
  macOS/Windows, and Linux's system qpdf links GnuTLS, not OpenSSL).
- **A over building both channels (Fork 2):** ships the owner's actual
  near-term need (their own dogfood Mac) without speculative work against a
  stable-channel signing step that doesn't exist yet in `release.yml`. The
  UI is shaped so Stable is a flag flip away, not a redesign — satisfies
  the "office/older-careful" persona's wish for the feature to land sooner.
- **A over GitHub Pages (Fork 3):** the distribution-cost objection is
  concrete and dominant — one publish surface (Releases, already used for
  `uat-summary.json`) is strictly less to maintain than two.
- **B over silent auto-install (Fork 4):** the power-migrator objection is
  real (this is genuinely more friction than the ideal), but the two
  admissible objections against A — an unattended write to the application
  directory, and the specific mid-swap-failure risk on macOS — are the
  kind of failure PHILOSOPHY's explicit-opt-in language exists to prevent,
  and the owner confirmed the safer default. The mid-swap risk is mitigated
  (not eliminated) by `UpdateManager::installAndRelaunch()`'s macOS
  sequence: the new bundle is staged fully (`ditto` to a `.update-staged`
  suffix) and verified in place *before* the running bundle is touched at
  all; the old bundle is renamed to `.old` (not deleted) until the new one
  successfully occupies the live path, and is removed only after that
  succeeds. See the accompanying PR body for the explicit, honest statement
  of what has and has not been exercised on real Gatekeeper hardware.

## Evidence required to reopen

- A concrete failure report from the owner's own dogfood use of the nightly
  channel (a swap that left a broken bundle, a Gatekeeper prompt the
  quarantine-clear step didn't suppress, a feed the checker failed to find)
  plus owner sign-off would reopen Fork 4 or the macOS install sequence
  specifically.
- A firm date for `release.yml` to grow its own signed-feed step would
  reopen Fork 2 (wiring the Stable channel for real).
- Evidence that GitHub's Releases API rate limits are actually being hit in
  practice (unauthenticated, 60 req/hr) would reopen Fork 3 toward a hosted
  or authenticated alternative.
