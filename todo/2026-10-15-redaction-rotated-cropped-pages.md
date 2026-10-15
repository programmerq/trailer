# Redaction leaves the text readable on rotated and cropped pages

Priority: P0. Tags: `security`, `data-loss`.

- Done when: red text under a redaction box leaves 0 red pixels in the saved file, and none
  of its text (text extraction finds none of it), at /Rotate 0, 90, 180 and 270 and on a page
  with a CropBox; the page keeps its orientation; links, form fields and other annotations
  outside the boxes survive; and a page that can't be rendered makes Save fail loudly instead
  of being skipped.
- Test: `test_known_bugs_pdf_files` (this entry). Not checked: the links, form fields and other
  annotations; a page that can't be rendered.
- Src: Red pixels before → after: unrotated 1,806 → 0; /Rotate 90 1,798 → 1,747 (readable,
  page upside down, box on blank paper); cropped 1,806 → 2,843; /Rotate 180 covered but the
  rotation is lost. The redacted page's text layer is gone, so searching the saved file won't
  reveal the leak. `PdfEditor::applyRedactions` (`PdfEditor.cpp:901-1016`) renders the page as
  displayed (rotation and CropBox applied) into a raster the size of the unrotated MediaBox,
  paints the boxes at displayed coordinates, then keeps /Rotate and /CropBox, so the rotation
  is applied twice. It also drops every /Annots entry on a redacted page (read in the code, not
  tested), and a failed render silently skips the page.
