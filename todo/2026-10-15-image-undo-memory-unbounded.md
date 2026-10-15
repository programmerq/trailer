# Image undo memory is unbounded by size

Priority: P2. Tags: `undo`.

- Done when: undo history per image stays under 4× the decoded image, with every step still
  undoable, and rotate and flip are stored as operations, not pixels. An edited image holds no
  full copy outside the history: today the finished decode keeps the originally decoded image
  alive until the document closes (`ImageAdapter.cpp:348-374`).
- Test: `test_known_bugs_image` (this entry): the heap held by 8 rotates and flips, and that each
  undoes. Not checked: the decoded image kept outside the history (the heap is measured after
  the decode). It needs glibc's `mallinfo2()` and skips elsewhere.
- Src: `kMaxUndoSteps = 32` full-resolution copies (`ImageAdapter.cpp:71`,
  `:1077-1080`): about 3 GB for a 24 MP photo.
