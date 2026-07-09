# DR-2: Duplicate "Manage ML Models" as a button in the Machine Learning tab

- Status: Accepted
- Date: 2026-07-09
- Area: Preferences pane (ROADMAP #8)

## Context

"Manage ML Models…" lives in the **Tools** menu; it opens a modal dialog listing
ONNX models with download / verify / "Never download" controls. The Preferences
**Machine Learning** tab collects the three `[ml.scheduler]` toggles (background
recognition, segmentation preload, run-on-battery). Should the model manager be
reachable from that tab as well?

## The debate

- **Office non-technical user.** "The ML tab tells me the app does 'recognize
  text' and 'background removal' things. Naturally I'd ask 'and where do those
  come from?' Having a 'Manage models…' button right there answers it. Otherwise
  I don't connect the toggle to the download screen buried in Tools." — concrete
  problem: the toggles and the models they govern are physically separated.
- **Older, careful user.** "As long as the button just *opens* a manager and
  doesn't itself download or delete anything without me choosing, I'm
  comfortable. It's not destructive." — concrete problem: none; the action is
  non-destructive and only launches a further dialog.
- **Power migrator.** "I already know Tools → Manage ML Models. Adding a second
  door is harmless; just don't take the Tools one away." — concrete problem:
  removal would break a known path (avoided by duplicating).
- **Occasional user.** "One button, opens a dialog, closes again. No confusion." —
  concrete problem: none.

## Arbiter verdict

**Duplicate** it as a "Manage models…" button on the Machine Learning tab; keep
the Tools entry. The action is non-destructive (it opens a manager; any actual
download/delete is a further deliberate step inside that manager), and it is
co-located with the ML toggles it directly relates to, resolving the
non-technical user's disconnect. When no host callback is wired the button is
disabled rather than dead — no lying control. Not a stalemate; no owner
escalation.
