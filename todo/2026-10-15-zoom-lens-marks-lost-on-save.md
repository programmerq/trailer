# Zoom Lens marks vanish when a PDF is saved

Priority: P0. Tags: `save`, `data-loss`.

- Done when: a Zoom Lens drawn on a PDF is still present and editable after Save and reopen.
  A stopgap PR may instead disable the tool on PDFs, with a tooltip saying it works on images;
  that stops the loss but closes nothing.
- Test: `test_known_bugs_pdf_save` (this entry). Not checked: editing the lens after reopen.
- Src: the store goes 1 → 0 on Save. `PdfEditor.cpp:674-678`.
