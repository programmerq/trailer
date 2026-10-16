# MainWindow is a 5,700-line god object, and documents are held by raw pointer

Priority: P2. Tags: `infra`.

- Done when: no file in `src/ui/` exceeds 1,500 lines, and nothing outside `DocumentView`
  caches a raw `IDocument*`. Documents are QObjects, held by QPointer or looked up per call. No
  `findChild<AnnotationOverlay*>` remains. Split controllers out as the subsystem under each is
  replaced, not as a separate big bang.
- Src: `onCurrentDocumentChanged` alone is 714 lines.
