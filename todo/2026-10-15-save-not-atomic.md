# In-place Save deletes the file before replacing it, breaks symlinks, and resets permissions

Priority: P0. Tags: `save`, `data-loss`.

- Done when:
  - killing the process at every step of a save (fault injection at each file operation)
    always leaves either the complete old file or the complete new one at the path;
  - saving through a symlink updates the target and leaves the link a link;
  - mode 0600 stays 0600, and extended attributes are preserved. (Other hard links to the
    file keep the old content: that is the accepted cost of an atomic replace.)
  - image saves get the same treatment (write a temp file in the same directory, then rename).
- Test: `test_known_bugs_pdf_files` (this entry; symlink and mode). Not checked: fault injection,
  extended attributes, image saves.
- Src: The order is `unlink(target)` → `link(temp, target)` → `unlink(temp)`, so the
  path is empty in between; with TMPDIR on another filesystem the bytes are copied while it
  is empty. Every save leaves mode 0644, so a 0600 file becomes world-readable and a 0664 file
  loses group write. If the step after the remove fails, `restoreOnFailure`
  reloads the deleted path and the document vanishes. `PdfAdapter.cpp:2609-2610`, `:2663-2682`;
  `TempPath.h:24-25`; `ImageAdapter.cpp:1384-1391` writes in place.
