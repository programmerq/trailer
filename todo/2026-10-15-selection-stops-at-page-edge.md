# Text selection stops at the page boundary

Priority: P2. Tags: `text`.

- Done when: a drag from page 1's last line to page 2's first line copies both lines in order,
  and a drag from mid-word copies from that glyph.
- Src: `SelectableTextLayer.cpp` handles the current page only.
