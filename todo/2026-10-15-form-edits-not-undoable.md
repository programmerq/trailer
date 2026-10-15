# Form-field edits can't be undone

Priority: P1. Tags: `undo`.

- Done when: type in a form field, ⌘Z: the previous value is restored; ⌘⇧Z re-applies it.
- Src: `PdfAdapter.cpp:2817-2825` marks the document dirty and records nothing.
