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

## How Trailer reduces friction

The reference user opens a file, does the thing, and closes the file.
Anything between them and "the thing" is friction. Trailer pays the
implementation cost so the user doesn't pay the attention cost.

- **A control that won't work is greyed out, with a tooltip.** If a
  menu item, button, or option can't act in the current context — a
  document is the wrong type, an ML model is set to Never Download,
  an editing operation isn't supported by the format — disable the
  control and set a tooltip explaining where to go instead. Don't
  let the user click and meet a popup that says "actually no." A
  greyed-out item carries the same information without interrupting.
- **A popup is a last resort.** Use them for irreversible actions,
  for one-time consent (model downloads, redaction warning), or for
  errors that can't be self-evident from the UI state. Never use a
  popup to explain why a feature isn't available right now — that's
  what disabled state plus tooltip is for.
- **State changes are reflected immediately.** If the user flips a
  policy in one dialog, the menus and toolbars that depend on it
  update before the next user action — no "now restart the app" or
  "now reopen the document."

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

Until then, calling Trailer "1.0-ready" is a sign of the dev's
optimism, not the codebase's maturity.
