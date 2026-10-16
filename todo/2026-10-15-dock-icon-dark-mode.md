# The Dock icon doesn't follow dark mode

Priority: P3. Tags: `platform`.

- Done when: on a real Mac in dark mode (Tahoe), the Dock icon shows the dark variant and flips
  when Appearance is toggled.
- Src: the dark variants in `resources/macos/Assets.xcassets/AppIcon.appiconset` compile
  without a warning, but none reach `Assets.car` (nightly macOS log, 2026-08-02): a macOS
  `.appiconset` has no light/dark axis. The likely fix is an Icon Composer `.icon` source,
  which needs Xcode 26+. First run `scripts/diagnose-appicon-dark-variants.sh` on a Mac to
  rule out `assetutil` simply not listing the variants. Build wiring: `CMakeLists.txt:684-742`.
  History: `git show 09bc1e3:docs/backlog/2026-07-17-adaptive-dock-icon-option-b.md`.
