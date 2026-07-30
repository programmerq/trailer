# TweetNaCl — vendored third-party source

- **Upstream:** https://tweetnacl.cr.yp.to/ (Daniel J. Bernstein, Bernard
  van Gastel, Wesley Janssen, Tanja Lange, Peter Schwabe, Sjaak Smetsers).
- **Version fetched:** the `20140427` release, downloaded verbatim from
  `https://tweetnacl.cr.yp.to/20140427/tweetnacl.c` and
  `https://tweetnacl.cr.yp.to/20140427/tweetnacl.h` on 2026-07-30. Files
  are byte-for-byte upstream — do not hand-edit them; patch via a wrapper
  instead (see `src/update/Ed25519.{h,cpp}`).
- **License:** public domain. TweetNaCl is explicitly released into the
  public domain by its authors (see https://tweetnacl.cr.yp.to/index.html,
  "TweetNaCl is public domain software"); no attribution or license text
  is legally required, but this NOTICE records provenance for
  `THIRD_PARTY_LICENSES.md` bookkeeping and for anyone auditing the
  update-signing code path.
- **Why vendored instead of an already-linked crypto library — checked,
  not assumed:** Before adding this, the question was whether Trailer
  already links an ed25519-capable crypto library transitively through
  qpdf (which does support an OpenSSL or GnuTLS crypto provider) and
  could verify with that instead of adding a new dependency. It does
  not, on any of the three shipped platforms:
  - **macOS:** `scripts/build-macos.sh` builds qpdf with
    `-DUSE_IMPLICIT_CRYPTO=OFF -DREQUIRE_CRYPTO_NATIVE=ON
    -DREQUIRE_CRYPTO_OPENSSL=OFF -DREQUIRE_CRYPTO_GNUTLS=OFF`
    (`scripts/build-macos.sh:328-331`) — qpdf's own header-only native
    crypto, deliberately linking neither OpenSSL nor GnuTLS.
  - **Windows:** `docker/windows/Dockerfile:215-217` and
    `.github/actions/setup-windows-cross/action.yml:365-366` build qpdf
    with the same `REQUIRE_CRYPTO_GNUTLS=0 REQUIRE_CRYPTO_OPENSSL=0
    USE_IMPLICIT_CRYPTO=1` — again neither linked, "so we avoid pulling
    openssl/gnutls into the [build]" per the Dockerfile's own comment.
  - **Linux:** qpdf comes from the distro package (`libqpdf-dev`), which
    is dynamically linked, not statically bundled, and is NOT OpenSSL —
    `ldd` on Ubuntu noble's `libqpdf.so.29` shows `libgnutls.so.30`, not
    `libcrypto`. Even where present this is an accidental transitive
    runtime dependency of a shared library qpdf loads, not something
    `CMakeLists.txt` calls `find_package()` on or exposes headers for —
    using it would mean adding a real new direct dependency (GnuTLS,
    with no ed25519 guarantee across the distro's build) on this one
    platform only, while still needing a *different* answer on macOS/
    Windows.

  So no platform has a reliable, already-linked, header-accessible
  ed25519 implementation in Trailer's own dependency graph today; macOS
  and Windows go out of their way to link neither OpenSSL nor GnuTLS at
  all. Vendoring a small, public-domain, verify-only, dependency-free
  primitive is therefore the smaller footprint, not a bigger one — it
  adds ~800 lines of source under version control instead of a new
  linked library dependency on all three platforms (one of which would
  still need to be a *different* library per platform). CI-side feed
  signing uses the `openssl` CLI tool in GitHub Actions
  (`scripts/sign-update-feed.sh`) — that is a build-host tool
  invocation, unrelated to whether OpenSSL is linked into the shipped
  `Trailer` binary, and does not change this analysis.
- **What Trailer uses from it:** only `crypto_sign_open` /
  `crypto_sign` (ed25519) and `crypto_hash_sha512` (a dependency of
  ed25519 internally). `crypto_sign_keypair` is linked in (the object
  file is not function-split) but is only *called* from test-only code
  that generates throwaway fixture keys — see the top-of-file comment in
  `src/update/Ed25519.h`. The shipped app never generates a keypair.
- **`randombytes()`:** TweetNaCl declares `extern void randombytes(u8 *,
  u64)` but does not implement it — callers must supply one. Trailer's
  implementation lives in `src/update/Ed25519.cpp` (backed by
  `QRandomGenerator::system()`), used only by the test-only keypair
  helper described above.
