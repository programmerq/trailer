# ⌥⌘Q prompts when a PDF has page edits plus a pending redaction or signature

Priority: P3. Tags: `save`.

- Done when: such a PDF is kept by ⌥⌘Q with no prompt. At launch it returns with both edits
  intact, the annotation still editable, and the document dirty.
- Src: `Application.cpp:847-849`.
