# Open Recent is a flat list of up to 50 entries

Priority: P3. Tags: `chrome`.

- Done when: Open Recent shows at most 10 entries, most recent first, open files included, as
  on macOS (decided). History depth is unchanged.
- Src: `Settings.h:233` (`m_recentMax = 50`); `MainWindow.cpp:5012`.
