# Dragging a page forward lands it one slot early, and no page can reach the end

Priority: P1. Tags: `annotations`.

- Done when: in a 4-page document, dragging page 1 in the sidebar to after page 2, after page 3
  and to the end puts it 2nd, 3rd and 4th; `movePage(0, 3)` makes it last. A move that leaves
  the order unchanged neither marks the document edited nor adds an undo step.
- Src: the sidebar's drop handler subtracts 1 for a forward move (`Sidebar.cpp:234`), and
  `PdfEditor::movePage` subtracts 1 again (`PdfEditor.cpp:153`) and rejects any target past the
  last index (`:146`). `movePage(0, 1)` and `movePage(1, 2)` change nothing but still mark the
  document edited and push an undo step.
