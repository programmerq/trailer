# Changing zoom mode loses your page

Priority: P1. Tags: `nav`.

- Done when: open a 200-page A4 PDF in a 900×700 window, choose Continuous and Fit Page, go to
  page 140, then choose Fit Width. `currentPage()` is 140, and the scroll position equals a
  settled go-to-page-140 under Fit Width. The same holds for every zoom-mode change and every
  switch among Continuous, Single and Two Pages, at dpr 1 and 2.
- Src: was `docs/backlog/2026-08-05-zoom-mode-change-drops-the-current-page.md`.
