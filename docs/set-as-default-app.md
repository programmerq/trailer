# Setting Trailer as your default PDF / image app

For dogfooding: once this is done, double-clicking a PDF or image opens
Trailer instead of Preview / Photos / your distro's default viewer, so you
naturally exercise the app you're building instead of forgetting to open it
manually.

This doc is written for **one person setting this up once, on their own
machine**, from a locally-built binary or a nightly download — not a
polished install-wizard experience. Steps marked **unverified** were
worked out by reading the packaging source, not by running them on that OS;
if you hit a snag, that's the first place to check.

## File types Trailer handles

From `ImageAdapter::supportedExtensions` (`src/document/ImageAdapter.cpp`)
plus PDF:

| Type | Extensions | Worth associating by default? |
|---|---|---|
| PDF | `.pdf` | **Yes** — Trailer's primary format. |
| Common raster images | `.png`, `.jpg`/`.jpeg`, `.gif`, `.webp`, `.bmp` | **Yes** — this is the dogfooding target: photos and screenshots you'd open casually. |
| TIFF | `.tiff`, `.tif` | Optional — associate if you actually open TIFFs; scanner/fax output sometimes expects a different default viewer. |
| Netpbm | `.ppm`, `.pgm`, `.pbm` | Leave alone unless you specifically work with these — rare outside imaging pipelines, and other tools that produce them often expect their own viewer. |
| Bitmap/pixmap sources | `.xbm`, `.xpm` | Leave alone — X11-era formats, essentially never double-clicked. |
| Icon | `.ico` | Optional — low-stakes either way. |

The Linux `.desktop` (`platform/linux/trailer.desktop`) and the Windows MSI
(`platform/windows/trailer.wxs`) both declare the full list above. Nothing
in the repo currently supports HEIC/RAW/OpenEXR (Phase 6, unstarted per
`ROADMAP.md`), so there's nothing to associate for those yet.

---

## macOS

**Unverified by author** — worked out from `resources/macos/Info.plist.in`,
`docs/packaging-macos.md`, and Apple's documented Launch Services / Gatekeeper
behavior; not run on a Mac in this session.

Trailer's `.app` bundle already declares `CFBundleDocumentTypes` for
`com.adobe.pdf` and `public.image` with `LSHandlerRank=Alternate` (see
`resources/macos/Info.plist.in`), which is exactly what's needed for Trailer
to show up in Finder's **Open With** menu and in **Get Info**. This applies
to every macOS build — local `scripts/build-macos.sh` output and the nightly
DMG both bake the same `Info.plist.in`.

1. Build or download `Trailer.app`:
   - Local: `scripts/build-macos.sh` → `build-macos/Trailer.app`, or
     `make release` → `dist/trailer-macos-arm64.dmg`.
   - Nightly: download `trailer-<tag>-macos-arm64.dmg` from the latest
     nightly GitHub Release.
2. **Move `Trailer.app` into `/Applications`** before doing anything else.
   Launch Services indexes apps from a handful of known locations
   (`/Applications`, `~/Applications`, the DMG's mount point doesn't count);
   running it straight from the mounted DMG or from `build-macos/` means
   Finder's Open With list may not see it reliably, or may lose track of it
   on the next build.
3. **First launch — clear the quarantine flag.** The app is unsigned and
   un-notarized (project policy: Trailer isn't enrolled in the Apple
   Developer Program — see `docs/packaging-macos.md` "Signing and
   notarization (deferred)"). Gatekeeper will refuse to open it with a
   "damaged" or "unidentified developer" dialog. Clear the quarantine
   attribute once:
   ```sh
   xattr -dr com.apple.quarantine /Applications/Trailer.app
   ```
   Then launch it once normally (double-click, or `open /Applications/Trailer.app`)
   so Launch Services registers it.
4. Set Trailer as the default for PDFs:
   - Right-click (or Control-click) any `.pdf` file in Finder → **Get Info**.
   - Under **Open with:**, choose **Trailer.app** from the dropdown (if it's
     not listed, choose **Other…**, browse to `/Applications/Trailer.app`;
     since it's unsigned, you may need "All Applications" in the file-type
     filter, not just "Recommended Applications").
   - Click **Change All…** and confirm. This changes every `.pdf` on the
     system, not just that one file.
5. Repeat step 4 per image extension you want Trailer to own (`.png`,
   `.jpg`, etc.) — macOS's Open With / Change All is per-extension, not
   per-format-family, so there's no single "all images" switch.

**Optional scripted alternative:** [`duti`](https://github.com/moretension/duti)
(`brew install duti`) sets associations from the command line by UTI,
which is faster than repeating step 4/5 per extension:
```sh
duti -s io.github.programmerq.trailer com.adobe.pdf all
duti -s io.github.programmerq.trailer public.png all
duti -s io.github.programmerq.trailer public.jpeg all
# ...repeat per UTI you want
```
(Bundle identifier is `MACOSX_BUNDLE_GUI_IDENTIFIER` in `CMakeLists.txt`,
`io.github.programmerq.trailer`; confirm it matches your build with
`mdls -name kMDItemCFBundleIdentifier /Applications/Trailer.app` if `duti`
can't find it.) Not simpler than Get Info for a one-time single-machine
setup — mentioned because the task asks for it, and it's worth knowing
about if you're doing this across multiple Macs.

**Undo:** repeat step 4/5, choosing your previous default app (Preview for
PDFs/images) instead of Trailer.

---

## Windows

**Unverified by author** — worked out from `platform/windows/trailer.wxs`
and Windows' documented Settings/Default Apps behavior; not run on Windows
in this session.

Trailer's MSI installer (`scripts/build-windows-msi.sh`) registers Trailer
under `SOFTWARE\RegisteredApplications` with per-extension
`Capabilities\FileAssociations` and `OpenWithProgids` entries for the full
extension list above (`platform/windows/trailer.wxs` lines ~134-337) — this
is the standard "Default Programs" registration Windows expects, so once
installed via the MSI, Trailer shows up as a normal candidate in Settings.

**Important gap:** the nightly Windows artifact is a **plain ZIP**
(`nightly-trailer-windows-x86_64.zip`, see `.github/workflows/nightly.yml`),
not the MSI — it has none of the registry associations above. If you
downloaded the nightly zip, skip to the manual "Open with" steps below;
only an MSI install does the full registration.

### Path A — you have (or build) the MSI

1. Build it: `scripts/build-windows-msi.sh` (Docker) or
   `scripts/build-windows-msi.sh --no-docker` on a host with `wixl`
   installed → `dist/Trailer-<version>-Windows.msi`.
2. Run the MSI installer (double-click, or `msiexec /i Trailer-<version>-Windows.msi`).
3. Open **Settings → Apps → Default apps**.
4. Search for `.pdf`, choose it, pick **Trailer** from the list.
5. Repeat per image extension you want Trailer to own.

### Path B — nightly ZIP (or you just want the quick per-file way)

1. Unzip `nightly-trailer-windows-x86_64.zip` somewhere permanent (not
   `Downloads` or a temp folder — Windows remembers the exe path you
   associate, so if you move/delete it later you'll need to redo this).
2. Right-click a `.pdf` file → **Open with → Choose another app**.
3. Check **Always use this app to open .pdf files**.
4. Click **More apps**, scroll down, **Look for another app on this PC**,
   browse to `trailer.exe` in the folder from step 1.
5. Repeat per image extension.

This path works without any registry pre-registration — Windows lets you
point "Open with" at any executable — but it's per-extension, one at a
time, same as macOS.

**Undo:** Settings → Apps → Default apps → search the extension → switch
back to your previous default (Photos, Edge, whatever it was).

---

## Linux

**Verified in this repo** (inspected the built `.desktop` install path and
DEB packaging directly on this Linux box; associations not actually
exercised end-to-end in a desktop session).

`platform/linux/trailer.desktop` declares
`MimeType=application/pdf;image/png;image/jpeg;image/bmp;image/gif;image/tiff;image/webp;image/x-portable-pixmap;image/x-portable-graymap;image/x-portable-bitmap;image/x-xbitmap;image/x-xpixmap;image/vnd.microsoft.icon;`
and `Exec=trailer %F`. `CMakeLists.txt` installs this file to
`${CMAKE_INSTALL_DATADIR}/applications` (normally
`/usr/share/applications/trailer.desktop`) as part of `cmake --install`,
and the DEB's `postinst` refreshes the desktop/MIME databases
(`update-desktop-database`). So:

### Path A — installed via the DEB

1. Build it: `scripts/build-linux-deb.sh` (Docker) or
   `scripts/build-linux-deb.sh --no-docker` → `dist/trailer_<version>-1_amd64.deb`.
2. `sudo apt install ./dist/trailer_<version>-1_amd64.deb` (or
   `sudo dpkg -i` + `sudo apt-get install -f` for dependencies).
3. Set Trailer as the default handler:
   ```sh
   xdg-mime default trailer.desktop application/pdf
   xdg-mime default trailer.desktop image/png
   xdg-mime default trailer.desktop image/jpeg
   xdg-mime default trailer.desktop image/gif
   xdg-mime default trailer.desktop image/webp
   xdg-mime default trailer.desktop image/bmp
   # optional, per the table above:
   xdg-mime default trailer.desktop image/tiff
   ```
   Or use your file manager's per-file "Open With → set as default"
   equivalent (GNOME Files / Nautilus: right-click → Open With Other
   Application → Trailer → set default; KDE Dolphin: right-click →
   Properties → File Type Options).
4. Confirm: `xdg-mime query default application/pdf` should print
   `trailer.desktop`.

### Path B — local `cmake --install` (no DEB)

Same as Path A once installed — `cmake --install build` runs the same
`install(FILES platform/linux/trailer.desktop ...)` rule the DEB uses, so
step 3 above applies unchanged after:
```sh
sudo cmake --install build
```

### Path C — nightly tarball (no `.desktop` file included)

**Gap:** `nightly-trailer-linux-x86_64.tar.gz`
(`.github/workflows/nightly.yml` "Stage Linux artifact" step) contains only
the `trailer` binary and license text — it does **not** package
`platform/linux/trailer.desktop`. `xdg-mime default` has nothing to point
at until you install a `.desktop` file yourself. A backlog item tracks
fixing this at the source
(`docs/backlog/2026-07-30-nightly-linux-tarball-missing-desktop-file.md`);
until then:

1. Extract the tarball somewhere permanent, e.g. `~/apps/trailer/`.
2. Copy the repo's `.desktop` file and point `Exec`/`Icon` at your install:
   ```sh
   mkdir -p ~/.local/share/applications ~/.local/share/icons/hicolor/256x256/apps
   sed -e "s|Exec=trailer %F|Exec=$HOME/apps/trailer/trailer %F|" \
       platform/linux/trailer.desktop > ~/.local/share/applications/trailer.desktop
   cp resources/icons/trailer_256.png \
      ~/.local/share/icons/hicolor/256x256/apps/trailer.png
   update-desktop-database -q ~/.local/share/applications
   ```
3. Then run the `xdg-mime default` commands from Path A step 3.

**Undo (all paths):** `xdg-mime default <previous-app>.desktop <mimetype>`
(find your prior default first with `xdg-mime query default application/pdf`
*before* switching, so you have something to revert to — GNOME's default
PDF viewer is usually `org.gnome.Evince.desktop` or `evince.desktop`; KDE's
is usually `org.kde.okular.desktop`).

---

## About the dogfood-default gate (read before you flip the switch)

`AGENTS.md`'s **G8** gate defines the "dogfood-default milestone" as *"the
point at which Trailer becomes the maintainer's own default app for these
files,"* and that milestone is **owner-declared and observable** — it goes
live only when the owner adds a `dogfood-default` marker (a dated entry in
`ROADMAP.md`/the changelog, or a git tag named `dogfood-default`). Once
that marker exists, the full accessibility checklist
(`docs/accessibility-checklist.md`) becomes a real gate instead of the
dormant per-PR no-regress rule it is today (see the existing backlog item
`docs/backlog/2026-07-12-accessibility-dogfood-gate-marker.md`).

**Following the steps in this doc does not itself declare that milestone.**
You (the owner) making Trailer your personal default app is exactly the
real-world event the milestone describes, but G8 only activates when you
deliberately add the marker — not automatically because a default-app
setting changed on your machine. Add the marker when you're ready to also
take on the accessibility-checklist commitment that comes with it, not
before.
