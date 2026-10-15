# The nightly Linux tarball has no .desktop file

Priority: P3. Tags: `packaging`.

- Done when: the tarball contains `trailer.desktop` and an icon, so
  `xdg-mime default trailer.desktop application/pdf` works after extraction with no hand edits.
- Src: `nightly.yml:548-552`.
