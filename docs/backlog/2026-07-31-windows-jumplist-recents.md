---
id: 2026-07-31-windows-jumplist-recents
title: Windows taskbar Jump List — native pre-launch recents surface (macOS Dock-menu parity)
priority: P4
status: open
source: macOS Dock-menu-and-icon session, 2026-07-31 (G4 platform-parity note)
created: 2026-07-31
---

## Threshold

Right-clicking (or hovering, per the taskbar convention) the pinned/running
Trailer taskbar icon on Windows shows a "Recent" Jump List category with up
to 10 entries, sourced the same way as the macOS Dock menu (RecentFiles,
most-recent-first, existence-filtered — see `RecentFiles::existingEntries`),
reachable even when Trailer is not running (`ICustomDestinationList` /
`SHAddToRecentDocs` persist the list to the OS, same shape as macOS's
`NSDocumentController`).

## Context

The macOS Dock-menu-and-icon PR (2026-07-31) gave macOS a native,
pre-launch "recent files" surface: right-clicking the Dock icon shows the
10 most recent files, backed by `NSDocumentController` so it works even
when Trailer's process is dead (`src/platform/DockRecents.h`,
`Application::refreshDockRecents`). Per G4 (`docs/platform-conventions.md`,
PHILOSOPHY → *Platform-native per OS*), a feature's **shape** should adapt
per OS without the feature being **dropped** on any OS — Windows' native
equivalent of a Dock menu is a **taskbar Jump List**
(`ICustomDestinationList` COM API, or Qt's `QWinTaskbarButton`/native
`SHAddToRecentDocs` for the simpler "recent" category); Linux has no
cross-desktop-environment equivalent (GNOME/KDE each have their own
partial, non-standard recent-files integrations, not a single API Trailer
could target the way it targets GNOME's header-bar convention elsewhere).

This item is the honest "not dropped, just not yet built" tracking pointer
for that gap — recents are NOT unreachable on Windows/Linux today (File >
Open Recent is on every platform, unaffected by the Dock-menu work), only
the OS-chrome-level, works-when-not-running surface is currently macOS-only.
Not filed as a blocking gap for the Dock-menu PR (a native Dock menu is
inherently macOS-specific chrome; Windows' Jump List is a distinct,
non-trivial COM integration that deserves its own scoped PR, not a rider on
a macOS-only change), but tracked here so the asymmetry doesn't go silent.

No Linux item is filed alongside this: there is no standard freedesktop.org
mechanism comparable to Jump Lists or NSDocumentController that a
GTK-agnostic Qt app can target across GNOME/KDE/etc. today.
