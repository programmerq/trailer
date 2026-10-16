# Scanned PDFs are never OCR'd automatically

Priority: P1. Tags: `ocr`.

- Done when:
  - opening an image-only PDF, with the OCR model installed and background recognition on,
    makes the visible page's text selectable with no user action;
  - a born-digital PDF triggers no OCR; a mixed PDF OCRs only its image-only pages;
  - the missing-model hint follows the same per-page rule;
  - Recognize Text… recognizes the whole document in the background, cancellable, instead of
    asking for a page range first.
- Src: `hasTextLayer()` returns `m_valid` (`PdfAdapter.h:139`), so the gates at
  `OcrController.cpp:76` and `:280-284` stop every PDF. A per-page `pageHasText()` exists at
  `PdfAdapter.cpp:1621` but runs on the GUI thread. `MainWindow.cpp:2818-2841`.
