# Reference-User Smoke Session

A short, repeatable observation session in which a person who is **not
the maintainer** opens a fresh Trailer build for the first time and
performs three small tasks. The output is a list of moments where the
app made the observer pause, hunt, or ask a question — i.e., where
friction leaked through into someone who hasn't been pre-trained on
the codebase.

This document defines the protocol. It is deliberately small. The
shorter it stays, the more likely it actually runs.

## What this is, and isn't

It is **not** unit or UAT testing. Those answer "does the code do
what the code claims to do." A smoke session answers "does the
*product* match the reference user from PHILOSOPHY.md once the
candidate build is in someone else's hands."

It is **not** a HITL pass. A HITL pass is the maintainer driving
their own daily workflow on a real build and writing down everything
that annoyed them — see the dated subsections of `TODO.md` for
precedent. HITL passes capture **power-user-on-Trailer** friction; a
smoke session captures **fresh-eyes-on-Trailer** friction. The two
sources tend to point at different bugs.

It is **not** beta testing. Beta testing watches behaviour over
weeks. A smoke session is one sitting, around 20 minutes, with a
fixed script.

The single artefact produced is a numbered list of observations,
appended to `TODO.md` under a dated subsection. Recommendations,
designs, or fixes don't live in that list — they get split out
afterward into PHILOSOPHY changes, new UAT slots, or branches.

## When to run

- Before every minor-version bump (e.g. 0.1 → 0.2). The session
  precedes the version tag; observations either become fix-before-tag
  items or get filed as deferred work.
- Before declaring 1.0 (per PHILOSOPHY.md's stability criterion that
  the user-visible surface has been stable for one or two cycles —
  smoke sessions are how we tell whether "stable" actually means
  "comprehensible to a first-time user").
- Opportunistic: whenever a willing non-maintainer is in the room
  with a Trailer build. The friction is cheap to capture and
  expensive to find any other way.

## Who runs it

The **observer** is someone who:

- Is not the maintainer, has not contributed code, and has not seen
  this build before.
- Uses computers for work — email, documents, web — but is not a
  developer. (If a developer runs it, they need to consciously
  suppress the urge to debug aloud.)
- Doesn't need to know what Trailer is for in advance. The script
  starts by telling them.

The **note-taker** is anyone with a keyboard. They do not narrate,
correct, or hint. If asked "what should I do?" the answer is "do
whatever you think you'd do." The point is to learn what the app
makes obvious; intervention destroys the signal.

## Setup

- **A fresh build.** Released DMG / installer / DEB if one exists,
  otherwise the output of `scripts/build-macos.sh` / `scripts/build.sh`
  on the candidate branch.
- **A clean profile.** No prior `settings.toml`, no signature folder,
  no recent files. On macOS this means deleting `~/Library/Application
  Support/Trailer` (or running under a fresh OS user). On Linux,
  `~/.config/Trailer` and `~/.local/share/Trailer`. On Windows,
  `%APPDATA%\Trailer`.
- **Three input files placed on the desktop** (or wherever the
  observer's OS calls home):
  1. A small text-bearing PDF (1–3 pages). A lease addendum, a bill,
     a school form.
  2. A scanned-image PDF that has no text layer. Something where the
     OCR offer should fire.
  3. A photo with a clear subject on a busy background — a candidate
     for Remove Background.

  These three cover the reference-user use cases from PHILOSOPHY.md
  ("bills, lease addenda, tax forms, court filings, insurance
  claims, HOA paperwork, the PDF a notary just emailed back") plus
  the two ML features that have post-2026-05 UX investment.

## The script

The observer is told, verbatim:

> "This is Trailer. It's for opening PDFs and images the way you'd
> open them in Preview or Acrobat. There are three files on your
> desktop. For each one I'd like you to: open it, do one obvious
> thing with it, and close it. Pick what 'one obvious thing' means
> yourself. Talk out loud if you can. I'll be quiet."

That is the whole script. Three files. Three open-do-close cycles.
No further prompting except to repeat the instruction if asked.

## What the note-taker records

Per file, **observations** — what the observer *did* and *said*, not
what the note-taker thinks about it:

- Where did they click first? Did the click do what they expected?
- Did they pause? For how long, and looking at what part of the
  screen?
- Did they ask a question? Verbatim if possible.
- Did they discover a feature by accident, or hunt for one they
  expected to find?
- Where did they give up? On what affordance?
- Where did they say something positive without prompting? (These
  are signal too. They tell us what to *not* change.)

Behaviours beat opinions. "Clicked Tools menu, scanned it for ten
seconds, asked where the highlighter was" is data. "I think the
highlighter is hard to find" is interpretation.

If the observer volunteers a fix ("you should put a highlighter
button on the toolbar") record it as a *quote*, not a *finding*. The
finding is whatever made them want a fix.

## What to do with the list

After the session, the note-taker spends fifteen minutes converting
observations into one of three buckets:

1. **Defects.** Things that clearly broke or behaved against the
   user's reasonable expectation. These get a `TODO.md` entry under
   a dated `## YYYY-MM-DD smoke session` subsection, in the same
   format as the existing HITL passes.
2. **PHILOSOPHY revisions.** Recurring friction patterns that
   suggest the contract itself needs language. (Recent precedent:
   the "popup that says no" pattern was first observed via HITL
   passes, then codified in PHILOSOPHY's *How Trailer reduces
   friction* section once it had recurred enough.)
3. **Not actionable.** Observer preferences that conflict with
   PHILOSOPHY's stated direction, or behaviours that match an
   already-decided trade-off. These are still recorded — in a
   `Smoke session: not actionable` subsection of the same TODO.md
   entry — so future sessions don't surface the same data and
   surprise us.

A defect from a smoke session does not automatically jump the queue.
It is recorded as deferred work like anything else and ranked against
the rest of `TODO.md`. The lever isn't urgency; it's *visibility into
the reference user we already promised to serve*.

## Why this exists

PHILOSOPHY.md commits to a reference user who is not a developer.
Until something forces a non-developer to drive the app, every
decision is being made by someone who knows where the bodies are
buried. The HITL passes catch the maintainer's friction; the smoke
session catches everyone else's. Both are required to know what
Trailer actually feels like.
