# Copy Page as Image leaves out the page's marks

Priority: P1. Tags: `text`.

- Done when: Edit ▸ Copy Page as Image on a page with a rectangle and a text box puts an image
  on the clipboard that shows both marks, at page size × screen dpr. Checked at dpr 1 and 2.
- Src: `MainWindow.cpp:3672-3685` uses `renderThumbnail`, which paints the raw page.
