# A second drag-crop lands offset

Priority: P2. Tags: `annotations`.

- Done when: on a page whose CropBox is already smaller than its MediaBox, a dragged crop
  yields a CropBox equal to the drawn rectangle within 1 pt. Checked at dpr 1 and 2.
- Src: `PdfEditor.cpp:199-231`; `MainWindow.cpp:2257-2295`.
