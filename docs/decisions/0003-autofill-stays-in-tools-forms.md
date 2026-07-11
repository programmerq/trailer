# DR-3: AutoFill stays in Tools → Forms, not surfaced in Preferences

- Status: Accepted
- Date: 2026-07-09
- Area: Preferences pane (ROADMAP #8)

## Context

ROADMAP #8 lists AutoFill among the "scattered menu locations" alongside Reset
Trailer Settings and Manage ML Models, inviting the reader to consolidate all
three into Preferences. AutoFill ("AutoFill from My Card") lives in Tools →
Forms: it reads the active My Card and writes its values into the **currently
open PDF's** form fields, then enables Fill-Forms mode. The question: does it
belong in the Preferences window?

## The debate

- **Office non-technical user.** "I'd open Preferences expecting to *set up* my
  card once. But this button changes the document in front of me — if there's no
  document, what would it even do? That's not a settings feeling." — concrete
  problem: it mutates a document, not a preference.
- **Older, careful user.** "A button in a settings window that edits my open file
  is alarming. Settings should be safe to poke at. This isn't." — concrete
  problem: it would perform a document mutation from a place users treat as
  consequence-free.
- **Power migrator.** "In every app I've used, 'autofill this form' is a document
  command, near the form tools — not in global preferences. Managing the *card
  data* could be a settings pane someday, but the *fill* action is not a
  setting." — concrete problem: category error; it is a document verb, not a
  configuration noun.
- **Occasional user.** "Half the time I open Preferences there's no PDF loaded, so
  the button would just be greyed out and confusing." — concrete problem: it
  would be disabled most of the time the window is open.

## Arbiter verdict

**Keep as-is** in Tools → Forms. Unanimous across all four personas: AutoFill is
a document action (it mutates the current form), a category error inside a
settings window, and would be disabled whenever no form document is open. The
*management* of AutoFill cards is a separate concern that could earn a Forms pane
later (DESIGN §6.13); the *fill* verb does not. All four personas agreed, so
there was nothing to escalate.
