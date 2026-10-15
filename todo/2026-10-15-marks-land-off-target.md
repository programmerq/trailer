# Marks and text selection land on different content than the user pointed at

Priority: P0. Tags: `text`, `annotations`, `data-loss`.

- Done when, through MainWindow with real mouse events at positions found from rendered pixels,
  for {Fit Page, Fit Width, 100 %, 50 %, 400 %} × {Single, Continuous, Two Pages} × logical
  DPI {72, 96} × dpr {1, 2} × {document shorter and taller than the viewport}:
  - a drag with the default tool from 2 px inside a word's left edge to 2 px inside its right
    edge puts exactly that word on the clipboard;
  - a rectangle drawn 6 px outside the word, then saved, is stored containing the word's
    bounds with ≤ 2 pt slack per side, and no side lies further outside the word than those
    6 px (plus 1 px for rounding the ink box, in points at the zoom shown) plus 2 pt.
- Test: `test_known_bugs_window` (this entry): Fit Page, Fit Width and 100 % in Continuous on a
  one-page document, at DPI 96 and 72, and at dpr 2 with DPI 96. Not checked: 50 % and
  400 %; Single and Two Pages; dpr 2 at DPI 72; each zoom with a document both shorter and
  taller than the viewport (the page fits at Fit Page and overflows at Fit Width and 100 %).
- Src: In the default view (Fit Page) a drag across "INVOICE" copies "VOICE" at 72
  DPI and nothing at 96, and a box drawn around it is saved 31 pt right and 25 pt up. Fit Width
  is wrong at both DPIs; 100 % is right at 72 DPI and wrong at 96. On Windows and Linux every zoom is
  wrong, by about 750 px on page 3. `PdfAdapter.cpp:858-923`: page points are mapped with
  `zoomFactor()`, which stays 1.0 in fit modes; QPdfView also scales by
  `logicalDotsPerInch / 72`, which `src/` never reads; and an `extraY` term assumes vertical
  centring that QPdfView doesn't do.
