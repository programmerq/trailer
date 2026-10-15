# Packages ship a placeholder maintainer and homepage

Priority: P2. Tags: `packaging`.

- Done when: no `TODO` placeholder such as `TODO@trailer.example.com` remains under
  `packaging/` or `platform/`, and the DEB, RPM and AppStream metadata carry the real maintainer
  and homepage.
- Src: `packaging/deb/DEBIAN/control:7` and `copyright:3`; `packaging/rpm/trailer.spec:7`,
  `:61`, `:146` and `:148`; `metainfo.xml:10` and `:31`.
