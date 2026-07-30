---
id: 2026-07-30-nightly-linux-tarball-missing-desktop-file
title: Nightly Linux tarball doesn't ship trailer.desktop, so xdg-mime has nothing to point at
priority: unranked
status: open
source: docs/set-as-default-app.md authoring session 2026-07-30
created: 2026-07-30
---

## Threshold

`nightly-trailer-linux-x86_64.tar.gz` (staged in
`.github/workflows/nightly.yml`'s "Stage Linux artifact" step) includes
`platform/linux/trailer.desktop` (and an icon) alongside the `trailer`
binary, so `xdg-mime default trailer.desktop application/pdf` works after
extracting the tarball and running `update-desktop-database` on a
user-writable applications dir — without the user hand-copying and
`sed`-editing the `.desktop` file themselves.

## Context

While writing `docs/set-as-default-app.md` (instructions for making Trailer
the default PDF/image handler, per the owner's dogfooding motivation), found
that the nightly Linux artifact is a bare binary tarball — no `.desktop`
file, no icon, no MIME registration path — unlike the DEB
(`packaging/deb/`) and `cmake --install`, both of which install
`platform/linux/trailer.desktop` to `${CMAKE_INSTALL_DATADIR}/applications`
per `CMakeLists.txt`. A user downloading the nightly tarball (the workflow
this task explicitly optimizes instructions for) has no `.desktop` file to
register with `xdg-mime`. The doc ships a manual workaround (copy + `sed`
the repo's `.desktop`, install to `~/.local/share/applications`), but the
clean fix is for the nightly tarball itself to carry the `.desktop` +
icon so `xdg-mime default` works directly after extraction, matching what
the DEB/RPM install already do.

See also the companion Windows gap: the nightly Windows artifact is a plain
ZIP with none of the MSI's registry `Capabilities`/`OpenWithProgids`
associations (`platform/windows/trailer.wxs`) — same category of gap, not
filed separately here since the manual "Open with → browse to exe → Always"
path on Windows works without any packaging change (Windows doesn't require
pre-registration the way `xdg-mime` does), so it doesn't block the
instructions the way the Linux gap does.
