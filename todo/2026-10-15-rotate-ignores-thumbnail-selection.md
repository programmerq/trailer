# Rotate ignores the thumbnail selection

Priority: P2. Tags: `annotations`.

- Done when: with three thumbnails selected, Rotate Left rotates all three, and one ⌘Z
  un-rotates all three.
- Src: `MainWindow.cpp:2313-2344` rotates `currentPage()` only.
