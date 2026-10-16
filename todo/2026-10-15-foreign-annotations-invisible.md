# Annotations made by other apps are invisible in Trailer

Priority: P1. Tags: `annotations`.

- Done when: a /Stamp and a /Polygon with appearance streams show in the view, thumbnails, Copy
  Page as Image and print.
- Test: `test_known_bugs_pdf_files` (this entry; the view). Not checked: thumbnails, Copy Page as
  Image and print.
- Src: No file in `src` sets `QPdfDocumentRenderOptions::RenderFlag::Annotations`. Save
  and recovery keep these annotations; they are just never drawn.
