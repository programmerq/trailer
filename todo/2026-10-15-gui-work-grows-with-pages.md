# GUI-thread work grows with page count: layout, sidebar and thumbnails

Priority: P1. Tags: `open`.

- Done when these counting tests (no clocks) pass in Continuous, Single and Two Pages:
  - before the first frame, the pages touched are the visible ones plus the same fixed
    prefetch, for 1 and for 20,000 pages;
  - the GUI thread makes 0 engine calls across open, 3 scroll steps, 1 zoom, 1 resize and a
    mode switch;
  - page-record visits per scroll, zoom or resize event are at most visits(N=1) + 4·⌈log2 N⌉;
  - Two Pages relayout makes 0 `pagePointSize()` calls;
  - the GUI thread's instruction count to the first frame grows by fewer than 100
    instructions per page between 20,000 and 40,000 pages (callgrind, `--separate-threads`).
- Src: QPdfView's Continuous layout walks every page on open, zoom and resize; Trailer adds
  eight `pagePointSize` walks per page. `PdfAdapter.cpp:815-1471`; `TwoPageView.cpp:134-162`;
  `Sidebar.cpp:316`; `ThumbnailModel.cpp:190-218`. `tests/test_perf_page_geometry.cpp:103`
  asserts the O(pages) behaviour and must be inverted.
