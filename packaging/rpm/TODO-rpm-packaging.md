# RPM Packaging TODOs

- [ ] Consolidate `packaging/rpm/trailer.desktop` with `platform/linux/trailer.desktop` (created by DEB PR) into a single source once both PRs are merged
- [ ] Same for metainfo.xml and the placeholder icon
- [ ] Add real maintainer email and homepage URL
- [ ] Bundle Qt 6 libs properly (ldd walk) and list them in %files
- [ ] Submit to Fedora or COPR once app is more mature
- [ ] Add GPG signing to RPM
- [ ] Evaluate building for multiple arches (aarch64)
- [ ] Consider RPM spec for openSUSE (similar but minor differences)
- [ ] Add RPM build step to CI once Docker build is validated
