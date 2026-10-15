# OCR'd text can't be selected with the default tool, and the I-beam lies

Priority: P1. Tags: `ocr`, `text`.

- Done when, in the app's default state after OCR completes on an image and on a scanned PDF
  page, driven through MainWindow with real events at positions found from pixels:
  - hovering a word shows the I-beam, and the I-beam shows only where a drag would select;
  - a drag across two words highlights exactly those glyphs;
  - ⌘C and Edit ▸ Copy copy exactly those words;
  - Find highlights a match on the glyphs.
- Src: The overlay takes the mouse with the default tool (Select), and
  its Select path asks only the PDF text layer; OCR text selects only with tool None, which
  the app never sets. `MainWindow.cpp:4045`; `MarkupToolbar.cpp:43`, `:170`;
  `AnnotationOverlay.cpp:104-120`; `PdfAdapter.cpp:958-1027`.
