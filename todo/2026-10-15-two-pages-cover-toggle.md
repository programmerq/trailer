# Two Pages has no cover toggle

Priority: P2. Tags: `nav`.

- Done when: a View menu toggle switches Two Pages between two layouts and re-lays out live.
  One shows page 1 alone, then [2,3], …; the other pairs [1,2], [3,4], …. The choice survives a
  relaunch.
- Src: `TwoPageView.cpp:66` (`setCoverAlone` has no caller).
