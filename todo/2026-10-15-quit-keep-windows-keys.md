# Quit and Keep Windows answers to the wrong keys

Priority: P2. Tags: `platform`.

- Done when: ⌥⌘Q quits and keeps windows on macOS, also when the native Option-swap menu item
  isn't installed; on Windows and Linux the shortcut follows PRODUCT.md's Ctrl-for-⌘ rule, and
  AltGr+Q on a German layout still types @ without quitting; a test checks the shortcut by its
  portable text.
- Src: the shortcut is built from `Qt::MetaModifier`, which is ⌃ on macOS and the Windows or
  Super key elsewhere, so today it is ⌃⌥Q and Win+Alt+Q (`Application.cpp:1699`,
  `MainWindow.cpp:1201-1202`). The test asserts the same constant
  (`test_quit_and_keep_windows.cpp:741-742`). Inferred from Qt's key mapping, not tried on a Mac.
