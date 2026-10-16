# The ML scheduler wakes every 30 s for as long as Trailer runs

Priority: P2. Tags: `ocr`.

- Done when: with no ML work queued, the scheduler's threads stay asleep through 60 s of idle,
  and a switch to battery power still pauses speculative ML work (tested through a seam that
  delivers a power-change event).
- Src: a thread polls the power source every 30 s (`MlScheduler.cpp:20`, `:41-58`), and the
  scheduler lives as long as the app (`Application.h:406`).
