# Fit Width plus navigation, view-mode toggle or resize can freeze the UI at 100% CPU

Priority: P0. Tags: `nav`, `data-loss`.

- Done when: these seeds of the random UI walk pass its per-step quiescence check at dpr 1 and
  2, and so do 200 fresh seeds:
  - the recorded seeds 720458898, 84349732 and 269782153;
  - the random-walk seeds 3116271505, 516731256, 1970968076, 1628323449 and 2603950034.
- Src: `tests/pbt/test_pbt_walk_min.cpp` (label `advisory`). Replay a seed with
  `TRAILER_PBT_SEED=<seed> QT_QPA_PLATFORM=offscreen ctest -R test_pbt_walk_min`, adding
  `QT_SCALE_FACTOR=2` for dpr 2; all eight seeds fail today. gdb shows the GUI thread looping in
  `QAbstractScrollAreaPrivate::layoutChildren_helper → postEvent`. Force-quitting loses
  unsaved work. No known-bug test yet: the seeds freeze with this repo's cloud toolchain and
  fonts, but nobody has checked them on CI's, and a known-bug test must fail the same way
  there.
