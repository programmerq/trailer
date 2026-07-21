---
id: 2026-07-20-nondestructive-structural-redaction-keep
title: Non-destructive structural-PDF keep snapshot so a pending redaction/signature can ride ⌥⌘Q without prompting
priority: P3
status: open
source: discovered landing the structural-PDF keep-fidelity FIX 2 (branch claude/structural-pdf-keep-fidelity); contrasting-persona pre-push review
created: 2026-07-20
---

## Threshold

A PDF that is **structurally dirty** (rotate / delete / move / insert / crop)
**and** also carries a **pending (un-applied) redaction or signature
annotation** is kept via ⌥⌘Q with **no prompt**, and on restore returns with:

- the structural edit intact,
- the redaction / signature **still an editable, removable annotation** (NOT
  burned into page content), and
- the document dirty, Save re-associated to the original path.

Verified by an offscreen round-trip test (mirror
`restoreCombinedStructuralAndAnnotationKeepsBothEditable` in
`tests/test_quit_and_keep_windows.cpp`, but with a `Redaction` /
`Signature` annotation) asserting the annotation survives as a live store
object of its original type after restore. Close this item by deleting this
file in the implementing PR and citing the item id.

## Context / Body

This is the **fuller fix** behind the prompt fallback that landed now
(FIX 2 on branch `claude/structural-pdf-keep-fidelity`).

`PdfDocument::writeRecoverySnapshot` — the serializer the ⌥⌘Q structural-keep
capture reuses to persist the edited-PDF blob — is **destructive** for
redaction and signature annotations: it calls
`PdfEditor::applyRedactions()` + `PdfEditor::flattenSignatures()`
(`src/document/PdfAdapter.cpp`, in `writeRecoverySnapshot`), which burn those
annotations permanently into page content. So a structural PDF that *also*
carried a pending redaction/signature would come back on restore with the
redaction **burned in** and no longer editable/removable — a silent
irreversible commit in a routine no-prompt flow, violating the never-worry-save
floor (ADR-0004).

The shipped mitigation routes exactly that combination to the per-doc prompt:
`Application::canDraftForKeep` returns false for a structurally-dirty PDF that
`hasPendingRedactionOrSignature()` (see `src/app/Application.cpp`), so it falls
back to the ⌘Q-style Save/Discard/Cancel prompt rather than being silently
kept-and-burned. This is correct and safe, but it is **one residual where ⌥⌘Q
still prompts** — narrow (structural + pending redaction/signature only) and
never lossy, but a divergence from the "⌥⌘Q must NEVER prompt" goal.

## Proposed fix

Produce a **non-destructive** keep snapshot for this combination: serialize the
structural edits into the blob while preserving redaction/signature as still-
editable `/Annot` objects (i.e. do NOT run `applyRedactions` /
`flattenSignatures` on the keep path). On restore, `recoverFrom` already
repopulates annotations as editable store objects, so the combination would
persist with full fidelity and no prompt. The care point is that the keep
snapshot must not leak the redacted content prematurely (a redaction that is
merely an annotation still shows the content beneath until applied) — which is
acceptable for an app-private keep blob under the app data dir, but must be
documented so the blob is never treated as a shareable export.

A structural PDF with only **regular** annotations (highlight / note / text /
shapes) already rides the blob path today and round-trips editable — that path
must keep working.
