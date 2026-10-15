# Saving a signed PDF silently invalidates the signature

Priority: P0. Tags: `save`, `data-loss`.

- Done when: a clean ⌘S on a PDF with a /Sig field leaves its bytes unchanged ([Save rewrites unedited files, and JPEG Save re-encodes and drops EXIF](2026-10-15-save-rewrites-unedited-files.md)); editing
  one shows an inline, non-modal notice that the signature will break, and Save defaults to a
  copy unless the user confirms.
- Src: Nothing in `src` handles /Sig or /ByteRange, and every Save is a full rewrite.
