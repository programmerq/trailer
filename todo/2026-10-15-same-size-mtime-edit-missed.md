# An external edit that keeps the size and mtime goes unnoticed

Priority: P3. Tags: `save`.

- Done when: an overwrite with different content of the same length, within the same second,
  is classified as a change. Checked by a headless `classifyExternalChange` test.
- Src: `ExternalChangeState.cpp:34`.
