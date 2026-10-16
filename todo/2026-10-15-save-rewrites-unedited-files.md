# Save rewrites unedited files, and JPEG Save re-encodes and drops EXIF

Priority: P0. Tags: `save`, `data-loss`.

- Done when: ⌘S on an unedited PDF or image leaves the file's bytes and mtime unchanged. Saving
  an edited JPEG keeps its EXIF (orientation reset to 1), ICC profile and resolution, and does
  not re-encode below the source quality.
- Test: `test_known_bugs_window` and `test_known_bugs_image` (this entry). Not checked: the ICC
  profile and resolution.
- Src: `onSave` has no dirty gate (`MainWindow.cpp:3153-3171`). A JPEG is
  re-encoded at Qt's default quality on every save (55,412 → ~12,400 bytes; error grows with
  each save), and EXIF goes from 5 tags to 0 (`ImageAdapter.cpp:1385-1387`).
