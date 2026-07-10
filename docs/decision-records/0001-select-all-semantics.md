# 0001 — Select All (⌘A / Ctrl+A) semantics in a document viewer

- **Status:** proposed
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-09
- **Date accepted / superseded:** —

## Context

Trailer is a document/image viewer that is also an editor: it shows PDF and
image content, and it carries text, pages, and annotations as selectable
things. The Edit menu includes **Select All**, bound to the platform-standard
shortcut (⌘A on macOS, Ctrl+A on Windows/Linux). What that command *selects*
in a viewer is undefined today — text? pages? annotations? — and the answer is
context-dependent in a way the user should be able to predict.

Apple's HIG defines Select All (⌘A) as "Highlights all selectable content in
the current document or text container"
(https://developer.apple.com/design/human-interface-guidelines/the-menu-bar,
and the standard-shortcuts table at
https://developer.apple.com/design/human-interface-guidelines/keyboards).
The Windows convention is the same verb — Ctrl+A = Select all
(https://learn.microsoft.com/en-us/windows/apps/design/input/keyboard-accelerators).
Neither platform says what "all content" means when the document has several
kinds of selectable content at once, which is exactly Trailer's case. The HIG
also warns: **do not repurpose a standard combo for a non-standard action, and
a standard menu item must be wired to its standard shortcut** — so ⌘A must not
be left inert or bound to something surprising.

This record does not pick the verdict; it lays out the options and the
checkable line each would establish.

## Options

- **A. Context-of-focus.** Select All acts on whatever surface holds keyboard
  focus: focus in the page text layer → select all text on the page/document;
  focus in the thumbnail/sidebar → select all pages; focus in the annotation
  layer → select all annotations. Matches "current container" literally.
- **B. Content-primary, fixed.** ⌘A always selects the document's primary
  content (text where the document has a text layer; otherwise the image),
  with a separate, discoverable command for "select all pages" and "select all
  annotations."
- **C. Escalating.** First ⌘A selects within the focused container; a second
  ⌘A widens to the whole document (as some editors do for paragraph → document).

## Personas debate

- **Office non-technical user:** Rarely presses ⌘A deliberately; more likely to
  hit it by accident. Whatever it does must be instantly undoable and must not
  look like the document changed. Fears "I selected everything and now I'll
  break it."
- **Older careful user:** Wants ⌘A to do the obvious, single thing and nothing
  clever. Escalating behaviour (Option C) is the kind of "it did something
  different the second time" surprise this lens distrusts.
- **Power migrator:** Has muscle memory from Preview / Acrobat. In Preview, ⌘A
  in a PDF selects text; in the thumbnail view it selects pages. Expects
  context-of-focus (Option A) and will read a fixed global behaviour as
  broken.
- **Occasional user:** Won't remember any special rule. Needs the result to be
  visible (highlighted text, outlined pages) so they can see what happened and
  recover.

## Admissible objections

- **Power migrator, reaching for text selection in a PDF:** if ⌘A is globally
  bound to "select all pages" (a naive Option B), the migrator presses ⌘A
  expecting to copy the page's text, gets a page selection, and copies nothing
  usable — a concrete failure against a real muscle-memory flow.
- **Occasional user, no visible result:** if ⌘A selects something with no
  on-screen indication (e.g. annotations that aren't visibly highlighted), the
  user cannot tell the command did anything and presses it repeatedly — a
  visibility-of-system-status failure.

### Rejected as naked preference

- "Escalating feels more modern." — rejected: names no user, step, or failure.

## Checkable threshold this record would establish

Whichever option is chosen, the record commits to a table of *(focused surface
→ what ⌘A selects → how the selection is shown on screen)* that is testable in
UAT: for each focused surface, pressing ⌘A produces exactly the specified
selection and a visible indication, and the standard shortcut is never inert
when there is selectable content. Acceptance evidence: a UAT case per row of
that table, plus a screenshot of each resulting selection state (gate G2).

## Arbiter verdict + rationale

<Open — status is proposed.>

## Evidence required to reopen

Once accepted: a documented flow where a real user (or persona) reaches a
selectable surface and the accepted mapping produces a wrong or invisible
selection, plus owner sign-off.
