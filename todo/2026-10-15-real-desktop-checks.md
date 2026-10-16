# Nothing is checked on a real Mac or a real Windows desktop

Priority: P2. Tags: `infra`, `platform`.

- Done when each of these passes once on a real Mac (arm64, macOS 14+), and the ones that
  aren't macOS-only also on real Windows 11, with the results in the closing PR's description:
  - reopening the last document, including after Quit and Keep Windows;
  - (macOS) with no window open, launching or activating Trailer shows only the Dock icon and
    the menu bar, with no panel or window;
  - (macOS) holding ⌥ swaps Quit for Quit and Keep Windows on the real Quit row;
  - a capture on a 2x display opens 1:1 at Actual Size, also on the second of two displays with
    different scales;
  - trackpad pinch-zoom at 2x leaves marks where they were drawn;
  - (macOS) From Window… opens the picked window as an untitled image; with no window open and
    Screen Recording denied, one modal appears whose Open System Settings button opens that
    setting;
  - (macOS) the Dock's recent-documents menu lists recent files, also when
    NSDocumentController isn't running;
  - (macOS) Help ▸ Feedback Report… appears once in the app menu with several windows open;
  - installing a nightly update.
- Src: `UpdateManager.cpp:215-221`.
