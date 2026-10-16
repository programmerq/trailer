# Password-protected PDFs go blank after any page operation, and Save misreports

Priority: P0. Tags: `save`, `data-loss`.

- Done when: with a 3-page AES-256 PDF opened with its user password, Rotate keeps
  pageCount 3 and the document dirty; Save returns true; reopening with the password shows the
  rotation; the recovery snapshot succeeds; redaction works.
- Test: `test_known_bugs_pdf_files` (this entry). Not checked: redaction; that the recovery snapshot
  opens with the password and holds the mark (the test checks only that it is written and not
  empty).
- Src: After Rotate, pageCount becomes 0 (blank view) and the document reads as clean; Save
  returns false even though the file it wrote is correct; the recovery snapshot fails too.
  `PdfEditor::save` keeps the encryption on its preview and scratch copies, and the viewer and
  scratch-editor reloads never pass the password (`PdfAdapter.cpp:2122-2125`, `:2690-2700`;
  `PdfEditor.cpp:933`). Owner-password-only files work.
