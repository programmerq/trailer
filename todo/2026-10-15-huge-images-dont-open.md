# Images too large for Qt's default 256 MB decode limit don't open

Priority: P1. Tags: `open`.

- Done when: a 96 MP JPEG and a 20,000 × 20,000 PNG open and display, decoding scaled when
  needed. The allocation limit is set explicitly, with a rationale comment.
- Src: `ImageAdapter.cpp:348-352`.
