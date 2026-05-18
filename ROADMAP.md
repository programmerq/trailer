# Trailer — Roadmap

The tactical Now / Next / Later view of where Trailer is going.

**Companion docs.** [DESIGN.md](DESIGN.md) has the long-form phase
spec (Phases 0–8) and is the *strategic* roadmap.
[TODO.md](TODO.md) is the live HITL punch list of pickable items.
[CHANGELOG.md](CHANGELOG.md) is what already shipped. This file is
the bridge — what's happening between releases at a glance.

**House rules.**

- **No version-number targets.** Per project policy, the version
  number falls out of what ships, picked at tag time with semver
  applied to the diff. Don't plan toward a specific "0.2.0" /
  "0.3.0" — talk about "next release."
- **No calendar dates.** Trailer moves fast enough that dates are
  either false precision or stale within a week. Sequencing comes
  from this doc; timing is whatever the maintainer feels.
- **One thing at a time.** Solo maintainer + AI-agent assistance.
  The roadmap is a queue, not a parallel plan. *Now* should hold
  whatever the next session picks up.
- **Roadmap edits are batched.** Update this doc when priorities
  shift meaningfully — not for every commit. Frequent thrash
  signals unclear strategy, not responsiveness.

Last meaningful update: post-0.1.0 ship (2026-05-16).

---

## Now

Committed for the next release window. Pickable in this order.

| Item | Why | Status |
|---|---|---|
| **Sparkle auto-update (macOS)** | Without it, every patch release is invisible — users have no signal a new version exists. Highest user-facing ROI per hour of work left. Needs: EdDSA keypair + safekeeping, appcast feed hosted on GitHub Pages, Sparkle linkage into the macOS bundle, "Check for Updates…" menu item, RELEASING.md amendment. Does NOT require Apple Developer enrollment — appcast signatures are EdDSA, independent of Apple. | Not started |
| **Continuous-mode annotation drift fix** | Real "Trailer is wrong" bug — overlay uses `pageNavigator()->currentPage()` so annotations only render correctly on the page Qt PDF reports as current; coordinates drift on other visible pages in Continuous view. **Scope first** — fix may need per-page overlay or a view-geometry query Qt PDF doesn't yet expose. | Not started |
| **House rule: every fix lands with a paired UAT slot** | Process, not a deliverable. Closes the coverage gap (~70 of 281 spec'd cases pinned, ≈25%) incrementally. Already in effect — the PDF page-op work landed with `uat_pdf_014/024/035/056` paired. | Live |

## Next

Planned, not yet started. Sequenced loosely — exact order picks
itself when *Now* clears.

| Item | Why | Notes |
|---|---|---|
| **WinSparkle auto-update (Windows)** | Sister project to Sparkle, **shares the appcast XML + EdDSA pubkey** — one signed feed serves both desktops. Natural follow-up once macOS Sparkle is wired. | Sequenced after macOS Sparkle |
| **Unified AnnotationStore + PdfCommand undo log** | Current `m_lastUndoSource` heuristic gets the common case right but breaks on interleaved annotation + page-op undo sequences. Newly relevant because the four PdfCommands landed in the previous release window made interleaving common. | Refactor; bigger than a single-session task |
| **FreeText /AP appearance streams** | Text / SpeechBubble annotations still rely on property-based fallback. Most viewers reconstruct fine, but /AP is the durable answer for cross-app round-trip. Needs a font resource + BT block in the page /Resources dict. | Non-trivial; ≈ one full session |
| **Image batch: multi-doc ThumbnailModel** | Single-window image batches share the tab strip today, but the original user ask was "use the thumbnail bar to flip between the 5 photos." `ThumbnailModel` is still 1:1 with one `IDocument`. | Tab-strip workaround acceptable until then |
| **Linux + Windows screenshot region pickers** | macOS uses `screencapture -i`; Linux falls back to `gnome-screenshot` if available; Windows is full-screen only. Cross-platform parity is a stated goal. | One implementation per platform |
| **Two-page view layout** | `m_twoPagesAction->setEnabled(false)` with a // TODO at `src/ui/MainWindow.cpp:639`. Small. | Pickable any time |

## Later

Directional. Inclusion here is a commitment to the *direction*,
not the *scope* or *timing*.

- **Flathub submission (Linux Flatpak).** Lowest-effort Linux
  auto-update story — Flatpak handles per-user updates and ships
  through Flathub's existing infrastructure. The most likely
  Linux distribution channel for a small project.
- **Intel Mac binary.** Either an ML-disabled-everywhere mode OR
  a third-party ONNX Runtime x86_64 bundle — both are sanctioned.
  Low priority; "build from source" is the standing answer until
  someone picks it up.
- **Phase 6 format / colour work.** HEIC, OpenEXR, RAW read
  support; lcms2 colour management (Assign Profile, Soft Proof);
  OCR-embed-on-PDF-export; Image Description / alt text. The ML
  core (background removal, Smart Lasso / Instant Alpha, OCR)
  already landed; this is the other half of the phase.
- **Phase 7 stretch.** 3D viewing (USD, Collada, OBJ, STL);
  scanner support (SANE / WIA / ImageCaptureCore); camera import
  (libgphoto2); photo location / map view; local search index +
  command palette.
- **Phase 8 polish.** Full keyboard shortcut audit; screen-reader
  audit; localisation framework + community translations;
  user-facing documentation site.
- **Path to 1.0.** Declared when on-disk formats and public APIs
  feel stable for one or two minor cycles without thrash, per
  PHILOSOPHY.md. Not a calendar event. Until then, breaking
  changes on minor bumps are permitted.

## Won't have

Explicitly off the table so they stop eating planning oxygen.

- **Apple Developer Program enrollment** ($99/yr). Deferred
  indefinitely by project policy — many OSS projects don't
  enroll. The `xattr -dr com.apple.quarantine` install
  instruction is the standing answer for macOS. Reopening
  requires a funding plan attached.
- **Notarized macOS builds.** Gated on the above.
- **Cloud sync, accounts, telemetry, premium tier, ads, model
  training on user content.** Non-negotiable per PHILOSOPHY.md.
  Any PR that brushes against these gets stopped at review.
- **Per-PDF AutoFill matcher tuning.** Direct field manipulation
  is the primary path; AutoFill is a demoted secondary feature.
  See TODO.md "AcroForm fields" section for rationale.
- **Apple Pencil / iOS / mobile builds.** Trailer is desktop Qt6
  widgets only.
- **Browser / web version.** Native only.
- **Replacing Photoshop / Acrobat / GIMP.** Trailer is a
  workbench for "open it, do one thing, move on" — not a full
  editor or forensic PDF tool.

---

## Risks

- **Sparkle appcast key management.** EdDSA private key needs to
  be (a) safely stored — losing it strands every existing user on
  whatever version they have, and (b) reachable from CI to sign
  each release's appcast entry. **Mitigation:** store in a
  password manager *and* a GitHub Actions secret; document
  recovery in RELEASING.md once Sparkle lands; consider rotating
  via a "new pubkey" appcast entry that the old key signs.
- **Continuous-mode overlay drift may be Qt-PDF-blocked.** The
  view-geometry information needed for a correct fix may not be
  exposed by Qt PDF's current API. **Mitigation:** scope before
  implementing; if Qt-blocked, decide between (a) per-page
  overlay widgets and (b) waiting on / contributing the Qt patch.
- **CHANGELOG discipline drift.** New convention as of the
  release-tooling pass — `[Unreleased]` is updated as features
  land, not in a panic at tag time. **Mitigation:**
  `scripts/release-notes.sh v$PREV..HEAD` reconciles from `git
  log` if entries get missed.
- **UAT slot coverage drift.** Tight coupling between fixes and
  UAT slots is a discipline issue, not a tooling one. **Mitigation:**
  the rule is now a stated house rule in
  [AGENTS.md](AGENTS.md); review checklist should ask
  "is there a UAT slot?" for every user-visible change.
