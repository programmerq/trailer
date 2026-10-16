# Filled form values rely on NeedAppearances

Priority: P0. Tags: `annotations`.

- Done when: a filled text field renders in pdfium with NeedAppearances off, and prints.
- Src: `PdfEditor.cpp:1402-1416` writes /V and sets NeedAppearances but builds no /AP.
