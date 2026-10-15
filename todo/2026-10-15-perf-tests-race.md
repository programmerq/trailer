# Perf tests skip or race instead of counting

Priority: P1. Tags: `infra`.

- Done when:
  - the counting tests in [The window waits for the whole PDF to load before its first paint](2026-10-15-first-paint-waits-for-load.md), [GUI-thread work grows with page count: layout, sidebar and thumbnails](2026-10-15-gui-work-grows-with-pages.md) and [Opening a large image or animated GIF blocks the GUI for the full decode](2026-10-15-large-image-decode-blocks-gui.md) run in PR CI without the `uat` label, each
    marked QEXPECT_FAIL until its fix lands;
  - the QSKIP at `test_perf_render_ordering.cpp:114` is gone;
  - `test_perf_lazy_open` passes 50 of 50 runs alongside a parallel build.
- Src: `test_perf_lazy_open` failed 2 of 45 runs under load, and 3 of 7 runs on 2026-10-15,
  once run alone. `formCapabilityProbeDoesNotParseSynchronously` expects no parse yet right
  after `annotations()` starts the background load (`test_perf_lazy_open.cpp:205`), but that
  worker's own parse bumps the same global counter (`PdfAdapter.cpp:507-522`). Recording the
  thread of the first parse would make the check race-free.
