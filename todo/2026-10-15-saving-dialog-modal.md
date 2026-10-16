# Every PDF Save opens a window-modal "Saving…" dialog

Priority: P1. Tags: `dialogs`.

- Done when: saving a PDF creates no dialog (test: no top-level QDialog appears between Save and
  completion). The document stays scrollable while it writes, and the edited indicator clears
  when the write lands. A save still running after a fixed delay shows progress without a
  modal (test it with an injected slow writer).
- Src: `MainWindow.cpp:3232-3236`: QProgressDialog, WindowModal, `setMinimumDuration(0)`.
