# Packaging TODOs

- [ ] ~~Add Apple Developer Team ID and signing identity to
      scripts/build-macos.sh~~ — deferred indefinitely by project
      policy; the `xattr` quarantine bypass is the standing install
      instruction. Reopen only with a funding plan attached.
- [ ] ~~Add notarytool call to scripts/build-macos.sh for
      notarization~~ — gated on the above; same deferral.
- [ ] Wire an ed25519-signed auto-update channel. Sparkle 2 + WinSparkle
      is the leading candidate (shared appcast XML + ed25519 pubkey
      across macOS + Windows). The requirement is the signed channel
      itself; the library is open. See [ROADMAP.md](ROADMAP.md) Now
      item 1 for the full requirement. Velopack is **not** a fit
      under the current no-Apple-Dev policy.
- [ ] ~~Add macOS packaging step to CI once signing secrets are
      set up~~ — n/a until / unless Developer ID enrollment is
      reopened; current macOS DMG pipeline produces an unsigned,
      adhoc-signed bundle which is the intended end state for now.

## Third-party LICENSE files in install rules

- [ ] **CMake install rules must ship upstream LICENSE / NOTICE
      files for every Apache-2.0 + MIT dep alongside the binary.**
      [`docs/audit-2026-05-19.md`](docs/audit-2026-05-19.md) §7
      LIC-CRIT-1: `THIRD_PARTY_LICENSES.md` enumerates the deps,
      but the build doesn't actually copy the upstream LICENSE
      files into the install tree. Apache-2.0 §4.1 requires
      attribution be carried with the binary; shipping a release
      without it is a legal exposure for downstream packagers.
      Concretely:
      - `cmake/OnnxRuntime.cmake` knows where ONNX Runtime's MIT
        LICENSE lives in the downloaded tarball — install that
        next to the deployed `libonnxruntime` / `onnxruntime.dll`.
      - qpdf's Apache-2.0 LICENSE — ship via the platform package's
        doc/share directory.
      - PaddleOCR's Apache-2.0 NOTICE for the bundled English
        dictionary (`resources/ppocr_en_dict.txt`) — same.
      - Qt's LGPL-3.0 text — already required by Qt's own
        licensing terms; verify it's in each platform's install.
      This is pre-first-release work; no release yet is downstream
      of this gap. Pair with the ed25519 auto-update channel work
      above so the first signed release ships compliant.
