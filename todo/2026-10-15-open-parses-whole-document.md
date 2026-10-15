# Every open runs a whole-document qpdf parse

Priority: P2. Tags: `open`.

- Done when: opening a PDF runs no qpdf parse before page one shows, and at most one per
  document. A 100,000-page file without annotations adds no qpdf memory until its first edit;
  today the parse costs 1.43 s of CPU and 265 MB.
- Src: `PdfAdapter.cpp:507`.
