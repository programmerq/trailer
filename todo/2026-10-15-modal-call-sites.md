# 55 modal call sites remain

Priority: P2. Tags: `dialogs`.

- Done when: every site in `tests/uat/census/modal-census.json`, except `main.cpp`'s event
  loop and the recorder's dev-only prompt, is gone or is a decision PRODUCT.md allows: a file
  or page-range pick, confirming irreversible loss, one-time consent, or an error the UI can't
  show.
- Src: `test_uat_modal_census` (label `uat`) fails when a site is added; after removing one,
  regenerate the golden with `TRAILER_CENSUS_WRITE=1`. The 55 sites in 17 files include
  `main.cpp`'s `app.exec()` and the recorder's dev-only prompt. A dialog made modal and then
  exec()'d counts twice: the model download progress (`ModelManagerDialog.cpp:140`, `:345`)
  and Recognize Text (`OcrResultsDialog.cpp:24`). Known noise: the Export As pre-dialog
  ([Image Export shows a filter dialog before the save panel](2026-10-15-image-export-filter-dialog.md)), the Recognize Text page-range modal ([Scanned PDFs are never OCR'd automatically](2026-10-15-scans-not-ocrd-automatically.md)), the Saving… modal ([Every PDF Save opens a window-modal "Saving…" dialog](2026-10-15-saving-dialog-modal.md)), "Model Not
  Available — install manually" (`ModelManagerDialog.cpp:130`).
