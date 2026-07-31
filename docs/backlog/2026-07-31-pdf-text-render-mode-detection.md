---
id: 2026-07-31-pdf-text-render-mode-detection
title: Detect PDF text render mode (visible vs. invisible Tr 3) for page-content diagnostics
priority: TBD
status: open
source: owner follow-up on PR #131 (Feedback Report image/page dimensions), 2026-07-31
created: 2026-07-31
---

## Threshold

Given a fixture PDF with one page produced by an OCR-behind-image pipeline
(e.g. `ocrmypdf`'s default output: a full-page scanned image XObject with an
invisible, PDF text-render-mode-3 text layer on top) and a second fixture
page with genuinely visible, born-digital text, a headless probe can tell
the two apart — i.e. it returns a different, correctly-labeled result for
each — without rendering the page to compare pixels. The Feedback Report's
"Current page text" field (`src/diagnostics/FeedbackReport.{h,cpp}`, added
in PR #131) can then say "invisible OCR text over a scan" and "visible
native text" as two distinct, confident states instead of the current single
"extractable PDF text, ingested into Trailer's selection layer (NOTE: this
cannot be told apart from an invisible OCR text layer...)" line.

## Context

While adding image/page dimensions to the Feedback Report (PR #131), the
owner asked for a related field: the report's existing `Has text layer: yes`
line was true for BOTH a normal born-digital PDF page and a scanned page
carrying an invisible OCR text layer baked in by an external tool — search
found text in both, but click-drag selection only reliably worked for one,
and the report gave no way to tell them apart. This bit the owner on a
365-page manual mixing native and scanned pages.

The PR added a best-effort, honestly-labeled decomposition
(`currentPageHasExtractableText` + `currentPageHasSelectableTextBlocks` in
`DocumentSnapshot`, see the field comments in `FeedbackReport.h`) that
distinguishes "no text," "text from Trailer's own OCR," and "extractable PDF
text not yet ingested" — but it explicitly does NOT distinguish visible
native text from an invisible (PDF text render mode 3) OCR layer baked into
the page by an external tool (ocrmypdf, Adobe Acrobat's "Recognize Text",
etc.), because:

- Qt PDF's public API (`QPdfDocument`, `QPdfSelection` —
  `/opt/qt/*/include/QtPdf/qpdfdocument.h`, `qpdfselection.h`) exposes no
  text-render-mode information. Its `getAllText()`/`getSelectionAtIndex()`
  extract text regardless of the `Tr` operator's value, so "extraction
  succeeded" reads identically for visible and invisible text.
- The only avenue for real render-mode detection is walking the page's raw
  content-stream operators via qpdf (`QPDFObjectHandle` /
  `QPDFTokenizer`, in the vein of `src/document/PdfEditor.cpp`'s existing
  `/Resources /XObject` walks) — tracking the graphics-state `Tr` value
  across `BT`/`ET` text blocks and every intervening operator. This is
  real, non-trivial content-stream-interpreter work, judged disproportionate
  to add just for an on-demand diagnostic field.
- A related, also-deferred signal: whether the current page is "dominated by
  a full-page image" (checking for a large `/Subtype /Image` XObject in
  `/Resources`) would need either the same qpdf content-stream work (to know
  placement/coverage) or forcing `PdfDocument::ensureEditorLoaded()` — the
  heavier, separately-lazy qpdf-backed parse used for page mutations — on
  every report generation. Also deferred here rather than forced.

If this item is picked up, the natural home is a new `IDocument`-level probe
(mirroring `pageHasText`) backed by a qpdf content-stream walk in
`PdfAdapter.cpp`, gated so it only runs when the qpdf editor is already
loaded (opportunistic) OR behind an explicit "deep inspect" ask rather than
every report click — the frugality trade-off from PR #131's review-before-push
pass should be re-examined at that point.

Also worth reading before starting: `claude/pdf-text-selection` (a
concurrent branch investigating PDF text selection from the click-drag side)
may independently need the same visible/invisible/OCR-layer distinction —
check whether it landed anything reusable first.
