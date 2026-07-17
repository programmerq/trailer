# Philosophy

This file is part contract, part filter. It describes how Trailer is
built and maintained, what stays in scope, and what gets a polite
"that's a different project." It is not a roadmap; it is a set of
constraints that make the roadmap possible.

If you are evaluating Trailer for daily use, this is also the
durability promise: the things below are true today and intended to
stay true. If a future maintainer walks them back, the new direction
deserves a fork under a different name.

## What Trailer is for

A document-first desktop tool for the kinds of files non-technical
people send each other and have to deal with: bills, lease addenda,
tax forms, court filings, insurance claims, HOA paperwork, the PDF a
notary just emailed back. The user opens it, reads it, marks it up
or signs it, and sends it back. The app's job is to disappear during
that flow.

The reference user is not a developer. They want to:

- Open a file and see it without configuring anything.
- Find the place to fill in or sign without hunting.
- Make a change and have it survive a save.
- Send the result back via the same channels they got it from
  (email, AirDrop, Messages, a folder in iCloud).

If a feature serves that user, it's in scope. If it serves a
developer, an enterprise compliance officer, or a real-time
collaboration scenario, it probably isn't.

## What Trailer will not do

These boundaries are decided. Saying no quickly to off-axis requests
is how the in-scope work stays good.

- **No ads, ever.** Not "tasteful" ads, not "sponsored" content, not
  newsletter promos with a tracker pixel. The build artefact you
  download is the build artefact you keep using.
- **No telemetry.** No anonymous usage stats, no crash uploaders that
  phone home, no "help us improve Trailer" toggles that default to
  on. If a future Trailer ever needs telemetry to debug a specific
  hard problem, it is opt-in, time-boxed, and the data lives only on
  the user's disk until they choose to send a specific dump.
- **No premium / pro tier.** Trailer is one product, not a
  feature-gated upsell ladder. If a feature is good enough to ship,
  it ships for everyone.
- **No accounts.** No sign-in, no "create a Trailer account to sync
  your card." Settings live in `settings.toml`; signatures live in a
  folder; that's the whole story.
- **No cloud sync built into the app.** Users who want their
  signatures or recent files synced across machines can put the
  Trailer config directory inside iCloud / Dropbox / Syncthing
  themselves. Building cloud-sync into the app means servers, means
  accounts, means recurring costs, means a monetisation pressure
  this project is structured to avoid.
- **No real-time collaboration.** Trailer is for one human at a time
  working on a file that already exists. If your workflow needs two
  people editing the same document live, you want Google Drive,
  Dropbox Paper, or Notion.
- **No DRM, no rights enforcement, no "can this user open this
  file."** PDF readers exist that ship that surface; Trailer is not
  one of them. If a file is open-able, Trailer opens it; if it
  isn't, the user is told why and Trailer gets out of the way.
- **No model training on user content.** The OCR / segmentation /
  background-removal models that ship with Trailer are pre-trained
  open-weight models downloaded once and run locally. Nothing the
  user opens is uploaded anywhere, by anything, ever.
- **No closed-source acquisitions.** If Trailer ever changes hands,
  the new maintainer inherits an MIT licence and these constraints,
  not the freedom to relicense. A fork is always possible; an
  enclosure is not.

## What Trailer is built on

These are not arbitrary choices. They are constraints that make the
"no telemetry / no servers / no accounts" stance affordable to
sustain.

- **Local-first.** All state lives on the user's disk. Settings,
  signatures, recent files, ML model weights, undo history. No
  network round-trip is required to do anything that doesn't
  literally involve the network (downloading a one-time model
  weight, sharing a file via the OS share sheet, etc.).
- **MIT licence.** Permissive, lets downstream packagers do what
  they need without negotiation. The licence file at the repo root
  is the operative one; any other licence claim elsewhere is a
  mistake to fix.
- **Cross-platform via Qt 6.** Same binary surface on macOS, Linux,
  and Windows. Native share-sheet integration where available, OS
  defaults where not.
- **No VC funding, no equity stack.** Trailer does not raise. It
  does not have an exit strategy. It does not have a growth
  metric. If sustaining costs ever exceed the maintainer's
  willingness to absorb them, donations / GitHub Sponsors are the
  fallback — never advertising, never premium tiers, never an
  acquihire.
- **Conservative feature surface.** Every feature is permanent
  maintenance burden. New features land when they're earned by a
  real workflow, not because a similar app has them. "We don't do
  that" is a complete answer.
- **Hand-tuned values stay hand-tuned; reasons live in code.** Trailer
  doesn't collect telemetry, doesn't A/B-test, and won't acquire the
  data that would let it auto-tune thresholds in the field. The
  consequence is that magic numbers — ML confidence cutoffs, badge
  trigger scores, debounce intervals, fit-zoom caps, hit-target
  sizes — are chosen by hand, and they stay chosen by hand. The
  durability rule is that every such constant lives next to a
  comment explaining what it represents, what range was tried, and
  what symptom would justify changing it. A future contributor
  rebalancing one of these numbers should be able to read the
  comment, agree or disagree with the trade-off as stated, and
  commit a new value with an updated rationale — not guess at
  intent from the variable name.
- **Frugal by construction.** Trailer inherits a Win9x-era frugality
  ethos: it should feel light on a machine that is not new. Minimal
  binary, modest resident memory, no ballooned dependency tree pulled in
  for one convenience. Three standing questions accompany any change that
  touches performance, memory, or size, and belong in the PR when they
  apply:
  1. *Is it optimized?* — is this the efficient way, or the first way
     that worked?
  2. *Can software meaningfully improve it?* — is there a real algorithmic
     or structural win available, or is this as good as it gets?
  3. *Are we trading CPU / RAM / size for design simplicity?* — and if so,
     is that trade named and worth it, rather than accidental?
  The one standing exception is the ML runtime (ONNX Runtime plus the
  U²-Net / MobileSAM / PP-OCRv3 weights): it is large by construction,
  mitigated because the weights download once on first use with consent
  rather than shipping in the binary. Concrete envelopes live in
  [`docs/performance-budgets.md`](docs/performance-budgets.md).
- **Platform-native per OS; adapt the shape, never drop the feature.**
  Each OS shapes commands differently — macOS puts them in a global menu
  bar (all commands present, the set never changes, unavailable items
  dimmed not removed); Windows uses an in-window menu bar / command bar
  plus right-click context menus with accelerators shown in labels and
  tooltips; GNOME/Linux favours a header bar for primary actions and a
  menu button for the rest. Trailer adopts each platform's native command
  surface rather than forcing one uniform look. But *portability of the
  feature is not negotiable*: the fact that an OS shapes a feature
  differently is a reason to adapt its shape, never a reason to drop it on
  that OS. No feature is gated by OS (DESIGN §2.1 goal 3). This is
  enforced as gate G4 in [`AGENTS.md`](AGENTS.md).
- **Distribution stays cheap and independent.** Trailer is not enrolled
  in the Apple Developer Program (the $99/yr gate on Developer-ID signing
  and notarisation), and does not plan to be. A signed auto-update channel
  is *deferred*, not designed out: the leading candidate is an
  ed25519-signed channel that does not depend on Developer-ID / Authenticode
  trust (see [`AGENTS.md`](AGENTS.md) §*Phase status* and DESIGN §12).
  The requirement is the signed channel; the specific library is not
  fixed.

## How decisions get made

When a feature request arrives:

1. **Does it serve the reference user above?** If no, decline
   politely.
2. **Can the user do it some other way that works?** If yes, point
   there. Trailer is not the only tool in their workflow and
   doesn't try to be.
3. **Does it fit the local-first, no-telemetry, no-accounts
   constraints?** If no, decline. There's no future "we can add
   that as a paid feature."
4. **Is the maintenance cost over the next decade smaller than the
   value to the reference user?** If no, decline.
5. If yes to all four, it's a candidate. It still has to wait its
   turn.

Conflicts of interest are handled by stating them. If Trailer ever
takes corporate sponsorship for a specific piece of work, the README
says so by name. If a contributor is paid by an interested party,
that lands in the commit message.

## How design decisions get adjudicated

The list above is the *scope* filter. Once something is in scope, a
second process decides how it should look and behave. That process is
adversarial on purpose: the fastest way to a design that survives real
users is to attack it before they do.

- **Personas are unranked adversarial lenses, not a priority order.**
  The four personas in [`DESIGN.md`](DESIGN.md) §2.5.2 — the office
  non-technical user, the older careful user, the power migrator, the
  occasional user — are lenses you critique *through*, not stakeholders
  you rank. None outranks another. A design has to survive all of them;
  it does not get to please the top one and dismiss the rest. Where
  §2.5.3 names two personas as a minimum critique set, that is a
  *coverage floor* (critique through at least these two), never a
  statement that those two carry more authority than the others.

- **The Arbiter.** Every non-trivial design decision has exactly one
  arbiter: the role that reads the persona critiques and the objections,
  weighs them, and issues the verdict that goes into the decision record.
  The arbiter is an **agent role, named per decision in that decision's
  record** (the record's `Arbiter:` field), not a fixed person; the owner
  may name a specific delegate for a specific decision. The arbiter's job is
  not to have the best taste in the room; it is to make the call *explicit
  and reviewable*, with the rationale written down.

- **The owner is escalation-only.** Routine decisions do not wait on the
  owner. The owner (programmerq) is the escalation-only override, invoked to
  break a genuine deadlock, to overrule an arbiter, or to sign off on
  reopening a settled decision — never the routine arbiter of gates.
  Designing so the owner is a bottleneck for ordinary work is itself a
  design smell.

- **The admissible-objection test.** An objection counts only if it
  articulates a concrete, checkable problem: it names a user or persona,
  a step in a real flow, and the failure that user would hit at that
  step. "The office user reaches step 3, sees two buttons that both look
  primary, and can't tell which one saves" is admissible. A naked
  preference — "I don't like this," "this feels off," "I'd have done it
  differently" — carries no weight and is recorded in the decision record
  as rejected, with that as the stated reason. The bar keeps debate
  grounded in what a user would actually hit, not in taste.

- **The decision-record (ADR) lifecycle.** Adjudicated decisions live in
  [`docs/decision-records/`](docs/decision-records/), one file each,
  named `YYYY-MM-DD-<slug>.md`, following `TEMPLATE.md`. Status moves
  **proposed → accepted → superseded-by**. An *accepted* record is
  settled: you build to it, you don't relitigate it. Reopening an
  accepted record requires **superseding evidence** — a concrete,
  checkable problem (by the same admissible-objection bar) that was not
  on the table when the record was accepted — *plus* owner sign-off. A
  record that is replaced points forward with `superseded-by: <YYYY-MM-DD-slug>`; the
  old file stays for the audit trail.

- **Every work item carries a checkable threshold, declared first.**
  Before work starts, the item states a pass/fail threshold — a number, a
  concrete behaviour, or a named budget/spec row. Without one, "Done" is a
  matter of opinion, and opinions don't gate. This is enforced as gate G1
  in [`AGENTS.md`](AGENTS.md).

- **UX-Done is a higher bar than Built.** Code that compiles and passes
  unit tests is *Built*. A user-visible change is *Done* only when the
  affected states have been captured from the app and checked against the
  declared threshold and the persona lenses (gate G2 in AGENTS.md, the
  per-milestone audit in DESIGN §2.5.3). Built is necessary; it is not
  sufficient.

## How Trailer reduces friction

The reference user opens a file, does the thing, and closes the file.
Anything between them and "the thing" is friction. Trailer pays the
implementation cost so the user doesn't pay the attention cost.

- **No lying controls.** A control never claims to do one thing and
  does another. This has two parts:
  - *A control that won't work is greyed out, with a tooltip.* If a
    menu item, button, or option can't act in the current context — a
    document is the wrong type, an ML model is set to Never Download,
    an ML model isn't downloaded yet, an editing operation isn't
    supported by the format — disable the control and set a tooltip
    explaining why and where to go instead. Don't let the user click
    and meet a popup that says "actually no." A greyed-out item carries
    the same information without interrupting. (Scope: this applies to a
    control that *exists* in the surface. It is not a mandate to add a
    disabled stub for every roadmap feature that has no UI yet — an
    absent menu item can't carry a tooltip, and shouldn't have to.)
  - *Silent nearest-equivalent substitution is forbidden — forever.*
    "Silently" here means one specific act: substituting a **different**
    behaviour and presenting it to the user as the one they requested.
    The user asks for X; the app quietly does the almost-X it can manage
    and lets the user believe they got X. That is never allowed, at any
    point in Trailer's life. Note this is *not* the same as dropping a
    result: quietly discarding a failed or low-quality result the user
    can simply retry (next bullet), and the documented PDF round-trip
    drop of unknown annotation subtypes on save (DESIGN §6.3.1), are
    *drops*, not substitutions. A drop gives the user nothing and lets
    them try again; a substitution gives them the wrong thing dressed as
    the right thing. Only the second is banned.
- **A popup is a last resort.** Use them for irreversible actions,
  for one-time consent (model downloads, redaction warning), or for
  errors that can't be self-evident from the UI state. Never use a
  popup to explain why a feature isn't available right now — that's
  what disabled state plus tooltip is for. And never use a popup to
  report "I tried and the result wasn't useful": drop the bad result
  and let the user retry. (This drop is permitted — the user gets
  nothing and can try again; it is not the banned *substitution* of a
  different result dressed as the requested one. See *No lying
  controls* above.) A popup that just says "no" is noise the user has
  to dismiss before continuing.
- **Prefer the document over the dialog.** When a feature has output
  to show or parameters to gather, surface them on the content itself
  before reaching for a modal — a drag-handle, a hover chip, a
  sparkle badge on a menu item, an inline status-bar link, a
  selectable layer painted over the page. Modal dialogs are reserved
  for genuinely up-front decisions (page range, file picker, "are
  you sure?") that the user wouldn't want to make implicitly. If the
  feature's output is text, it lands in the document; if the output
  is a yes/no signal, it's a badge; if the output is a long-running
  computation, it's a status-bar indicator. The recurring direction
  of travel is dialog → in-place, never the reverse.
- **State changes are reflected immediately.** If the user flips a
  policy in one dialog, the menus and toolbars that depend on it
  update before the next user action — no "now restart the app" or
  "now reopen the document."
- **Never worry about saving.** The reference user's promise at the top
  of this file — "Make a change and have it survive a save" — is a
  floor. The model is *continuous persistence* (edits are written to the
  local store as they happen, debounced to coalesce bursts) *plus an
  explicit Save that is always honoured* for the user who still wants to
  press it. The user should neither lose work to a forgotten save nor be
  nagged into one. This aligns with the platform-native document model
  (Apple's file-management guidance: "avoid making people take an
  explicit action to save" —
  https://developer.apple.com/design/human-interface-guidelines/file-management).
  - **Open decision (needs owner).** Whether never-worry-save is a
    **hard invariant** (continuous persistence is always on and cannot be
    turned off) or a **default with an opt-out** (the auto-save disable
    toggle in DESIGN §6.10.1 is a legitimate escape hatch for the
    older-careful user who "prefers explicit Save over auto-save") is
    *not settled here*. The live behaviour today is a default-with-opt-out:
    the disable toggle exists, and the debounce is
    `kAutoSaveIntervalMs = 30000` at `src/ui/MainWindow.cpp:392`. This
    tension is written up, unresolved, as decision record
    [`0004-never-worry-save-invariant`](docs/decision-records/0004-never-worry-save-invariant.md)
    (status: proposed) — the owner picks invariant vs. opt-out there, and
    this bullet is updated to match once they do.

## How Trailer fails gracefully

The thing that protects Trailer's quality from "let's monetise this
to keep the lights on" pressure is making the lights cheap. That
means:

- **No infrastructure.** No server to keep running. No CDN bill. No
  database. No mailing list provider. No analytics vendor.
- **No support tier promised.** Issues on the public tracker get
  best-effort attention from whoever cares. No SLA, no premium
  support contract, no reason to gate a fix behind a paying tier.
- **A fork is welcome, named differently.** If a future maintainer
  goes a direction the community doesn't follow, the codebase is
  MIT and the door is open. "Trailer" the trademark stays with the
  upstream; the code is anyone's to take and rename.

If Trailer ever stops being maintained and someone forks it under a
different name to keep going, that's a success, not a failure. The
goal is the workflow, not the project's name on a binary.

## What 1.0 means

Today's version is 0.1. While we're in 0.x:

- Breaking changes are allowed on minor bumps. The settings.toml
  format, the on-disk signature layout, even the IPC and the
  IDocument interface can shift between 0.1 and 0.2 if a better
  shape is found.
- "Stable" is a goal, not a claim. Don't depend on the wire
  formats yet.

1.0 will be declared when:

- A non-trivial number of users have lived with the app long enough
  to surface durability bugs (state corruption, weird file types,
  edge cases).
- The on-disk formats and the public APIs feel right enough to
  commit to backward-compatibility on minor bumps for the
  foreseeable future.
- The user-visible surface (menus, shortcuts, defaults) has been
  stable for at least one or two minor cycles without us thrashing.
- **A GUI Preferences pane is present.** A settings window a
  non-technical user can reach and operate (DESIGN §6.13), reachable
  from the standard platform location (⌘, on macOS; Edit/Tools →
  Preferences on Windows/Linux), is a hard 1.0 gate — settings that only
  a `settings.toml` editor can change do not count. Enforced as gate G7
  in [`AGENTS.md`](AGENTS.md).

One milestone precedes 1.0 and carries its own gate. The
**dogfood-default milestone** is the point at which Trailer becomes the
maintainer's own default application for these files, day to day, on
their own machine. This milestone is **owner-declared and observable**: it
is active once the owner records a `dogfood-default` marker — a dated entry
in [`ROADMAP.md`](ROADMAP.md) / the changelog, or a git tag named
`dogfood-default` — so an agent can tell objectively whether the gate is
live; until that marker exists gate G8 is dormant (per-PR no-regress only).
The accessibility surface (DESIGN §6.12) — keyboard-only
operability of every command, screen-reader labels, configurable text
size, high-contrast theme, and reduce-motion — is scheduled to be in
place *by* this milestone, not deferred to 1.0. Enforced as gate G8 in
AGENTS.md.

Until then, calling Trailer "1.0-ready" is a sign of the dev's
optimism, not the codebase's maturity.
