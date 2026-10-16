# Images at Fit are drawn at 1x and upscaled on HiDPI screens

Priority: P1. Tags: `open`.

- Done when: on a screen with dpr d, at any zoom, the displayed pixmap's device width is at
  least min(source width, drawn logical width × d) − 1. PDFs stay as crisp as they are today
  (fringe ratio within 25 % of a native render at dpr 1 and 2).
- Test: `test_known_bugs_image` (this entry, dpr 2 entry; Fit). Not checked: other zooms; PDF
  crispness. The test reads the pixmap the app hands its label (`labelPixmapForTest()`), not
  a grab of the screen.
- Src: A 4000×3000 photo at Fit gets a 997×748 pixmap drawn across 1,994 device px.
  `ImageAdapter.cpp:94-97`, `:117-127`.
