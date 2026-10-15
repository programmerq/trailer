# The search poll ticks every 150 ms forever

Priority: P2. Tags: `open`.

- Done when: with no search active, no timer fires (checked with a timer-event counter over 5 s
  of idle).
- Src: `MainWindow.cpp:244-263`; the comment says it only runs during a search.
