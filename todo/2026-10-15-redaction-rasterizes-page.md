# Redacting one word turns the whole page into a picture

Priority: P2. Tags: `annotations`, `text`.

- Done when: after redacting one word on a text page and saving, the rest of that page's text
  can still be selected and found in Trailer and in pdfium, and the redacted word can't be
  extracted by any tool (pdfium text, `qpdf --qdf` content streams). Images under a box are cut,
  not just covered.
- Src: `PdfEditor::applyRedactions` replaces the page's content with a raster of the page
  (`PdfEditor.cpp:901-1016`). Preview removes only what is under the box.
