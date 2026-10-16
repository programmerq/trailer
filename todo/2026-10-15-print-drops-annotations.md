# Print leaves out annotations, signatures and unsaved marks, and rasterizes on the GUI thread

Priority: P1. Tags: `print`.

- Done when: printing to PDF a document with a saved mark and an unsaved mark contains both;
  images print with their marks; no page is rasterized on the GUI thread while printing
  (counted at the render call).
- Test: `test_known_bugs_pdf_files` (this entry; PDFs). Not checked: images; rasterizing on the GUI
  thread.
- Src: `PdfAdapter.cpp:2052-2099` renders `m_doc` with default flags; `ImageAdapter.cpp:
  1517-1538` prints `m_image` without the overlay.
