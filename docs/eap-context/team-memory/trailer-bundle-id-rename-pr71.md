---
name: trailer-bundle-id-rename-pr71
description: Bundle/AppStream identifier renamed org.trailer.Trailer → io.github.programmerq.trailer, MERGED via PR #71 (2026-07-17); lists remaining org.trailer follow-up surfaces on unmerged branches
metadata:
  type: project
---

# Bundle id rename — io.github.programmerq.trailer (PR #71, MERGED 2026-07-17)

Renamed the reverse-DNS identifier from `org.trailer.Trailer` to `io.github.programmerq.trailer` (org.trailer.* falsely implied ownership of trailer.org; project lives at github.com/programmerq/trailer, no domain → `io.github.<user>.<app>` convention). Branch `chore/bundle-id-github`, commit `517fb48`, merged to main.

## What the rename touched (now on main)
- CMake `MACOSX_BUNDLE_GUI_IDENTIFIER` (Info.plist.in inherits via `${...}`).
- Both AppStream metainfo `<id>`s + **files renamed** to `io.github.programmerq.trailer.metainfo.xml` in `platform/linux/` and `packaging/rpm/` (AppStream requires filename == component id). `<launchable>` left as `trailer.desktop`.
- `packaging/rpm/trailer.spec` %install source/dest + %files metainfo paths.
- Qt identity: new static `Application::applyIdentity()` adds `setOrganizationDomain("programmerq.github.io")`; org/app NAME stay `"Trailer"`. New test `tests/test_application_identity.cpp`.
- Fixed stale homepage URLs `trailer-app` → `programmerq` in metainfo/spec/deb copyright; TODO-linux-packaging.md + build-macos.sh comment.

## Key facts (don't rediscover)
- **No settings migration** on any platform: the only QSettings consumer, `DocumentTypeDefaults` (src/settings/DocumentTypeDefaults.cpp:107/113), uses the 2-arg `QSettings(org, app)` ctor → keys off organizationName "Trailer", ignores organizationDomain. Nothing moves. `AppPaths` hardcodes "Trailer"/trailer. There is NO default-constructed `QSettings` anywhere.
- **macOS TCC caveat**: Screen Recording grant is keyed to the bundle id and will NOT carry over after rebuild; app re-prompts. Clean stale entry: `tccutil reset ScreenCapture org.trailer.Trailer`. `ScreenCapturePermission.cpp` does NOT hardcode the bundle id.
- **Deliberately unchanged**: `trailer.desktop` filename / `StartupWMClass=trailer` / icon names (none embed the rDNS id; icon/WM_CLASS assoc keys off binary name `trailer`). Windows keys off product name "Trailer" (registry `SOFTWARE\Trailer`) — no rDNS id.

## Remaining follow-up surfaces (still carry org.trailer on unmerged branches)
- Packaging PR `feat/packaging-release-flow` (see [[trailer-packaging-release-flow]]) edits the same metainfo/spec files → will need a rebase touch-up for the renamed files/ids.
- `feat/ux-recorder` (PR #69) `src/uxrecord/MacUxPlatformCapture.mm:285-286` uses GCD dispatch-queue labels `org.trailer.ux.screen` / `org.trailer.ux.camera` — a DISTINCT usage (not the bundle id), left untouched; could later become `io.github.programmerq.trailer.ux.*`.
- Desktop-file/Flatpak rename for a future Flatpak build (`$FLATPAK_ID` must match component id) is still a TODO in platform/linux/TODO-linux-packaging.md.

Process followed [[trailer-review-before-push-policy]] (test first, 2 variant-persona local reviews, then push+PR); remote SHA verified per [[trailer-verify-remote-after-push]].
