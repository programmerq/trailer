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
