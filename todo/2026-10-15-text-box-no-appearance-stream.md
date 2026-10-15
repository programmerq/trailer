# Saved text boxes carry no appearance stream

Priority: P0. Tags: `annotations`.

- Done when: a text box or speech bubble saved by Trailer has an /AP, so pdfium renders its
  text with annotations on. Also check it by hand in Preview.
- Src: `PdfEditor.cpp:620-626` writes /DA but no /AP.
