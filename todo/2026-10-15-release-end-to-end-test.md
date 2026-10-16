# Releases are untested end to end

Priority: P2. Tags: `packaging`.

- Done when: the release workflow produces every artifact from one tag; `dpkg-deb -c` on the
  `.deb` lists `libonnxruntime`, and `ldd` of the installed binary in a clean `ubuntu:24.04`
  container reports nothing "not found"; release notes name the right qpdf package
  (`libqpdf29t64` on 24.04).
- Src: `release.yml` has succeeded 3 times, last on 2026-07-13, and its `.deb`/`.rpm` steps
  have never run in a release. `scripts/bundle-qt-runtime.sh:60-64` drops libraries `ldd`
  can't resolve. `release-publish.yml:250` says `libqpdf28`.
