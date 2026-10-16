# PDF Save re-appends every annotation already in the file

Priority: P0. Tags: `save`, `data-loss`.

- Done when:
  - three Saves, each made after adding one mark, leave exactly 3 annotations in the file,
    counted with a fresh `PdfEditor::readAnnotations()`;
  - a file that arrives with 2 annotations still holds 2 after a clean Save, and after
    Rotate + Save; Save As behaves the same;
  - a file carrying other apps' annotations (a Highlight with /Popup, a reply with /IRT, /T
    author, /NM, /RC rich text, /CA opacity, a Link) keeps its annotation count and those
    keys, through three clean saves and one unrelated edit.
- The fix needs annotation identity: each annotation keeps its source object, and Save
  rewrites only new or changed ones and deletes only removed ones. Clearing all managed
  annotations and writing them again (what `writeRecoverySnapshot` does today) strips other
  apps' metadata, so it is not a fix; the recovery sidecar has the same loss today.
- Test: `test_known_bugs_pdf_save` (checks tagged this entry).
- Src: Each Save writes the store on top of what the file holds: three mark-and-save
  rounds leave 1, 3, then 7 marks; a clean ⌘S doubles them. Other apps' annotations keep their
  keys, but Save appends stripped copies of them (6 → 10 on the test file).
  `PdfAdapter.cpp:2596-2604`; `PdfEditor.cpp:1030-1110`; `Annotation.h:99-113` (no source object
  id, author or dates).
