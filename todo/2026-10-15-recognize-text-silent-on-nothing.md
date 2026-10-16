# Recognize Text that finds nothing within a second says nothing

Priority: P2. Tags: `ocr`.

- Done when: Recognize Text… on a page with no text always ends with a visible "No text found",
  however fast recognition finishes, including with the progress reveal delay at 0; and UAT
  `ocr_ev_20` passes 50 of 50 runs.
- Src: the batch reveal timer starts even at delay 0 (`OcrController.cpp:191`), and a batch
  that finishes first stops it (`:215`), so MainWindow never calls `finishWithMessage`
  (`MainWindow.cpp:940-944`). `ocr_ev_20` (`tests/uat/test_uat_ocr_evidence.cpp:244`) fails 6 of
  40 runs alone.
