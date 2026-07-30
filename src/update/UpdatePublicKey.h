#pragma once

#include <array>

namespace trailer::Update {

// The ed25519 PUBLIC key Trailer trusts to verify the nightly update
// feed (and, later, the stable feed). Embedding the public key in the
// binary — rather than fetching it from anywhere — is the whole point:
// a compromised release pipeline or feed host cannot swap in its own
// key, because the verifier never trusts anything it downloads to tell
// it which key to check against. See
// docs/decision-records/2026-07-30-nightly-auto-update-channel.md.
//
// *** THROWAWAY DEV KEY — DO NOT SHIP ***
// This is a placeholder keypair generated for development/testing only
// (see the PR body for the exact commands). Its PRIVATE half is not
// committed anywhere and was generated with a non-cryptographic PRNG —
// treat it as burned. Before this ships to real users, the owner must:
//
//   1. Generate a REAL keypair with a cryptographic RNG, offline:
//        openssl genpkey -algorithm ed25519 -out trailer-update-ed25519.pem
//        openssl pkey -in trailer-update-ed25519.pem -pubout -out trailer-update-ed25519.pub.pem
//      Extract the raw 32-byte public key bytes (not the PEM wrapper)
//      for the array below, e.g.:
//        openssl pkey -in trailer-update-ed25519.pem -pubout -outform DER | tail -c 32 | xxd -i
//      (DER Ed25519 SubjectPublicKeyInfo is 44 bytes; the raw key is the
//      last 32.)
//   2. Store trailer-update-ed25519.pem in a password manager AND as a
//      GitHub Actions secret (e.g. TRAILER_UPDATE_SIGNING_KEY, base64-
//      encoded) available to nightly.yml's feed-signing step — per
//      ROADMAP.md's key-management risk note. Never commit the private
//      key to the repository.
//   3. Replace kNightlyPublicKey below with the real public bytes and
//      remove this warning block.
//   4. Consider recording a "new pubkey, signed by the old key" rotation
//      entry if the key is ever rotated (ROADMAP.md's own suggestion).
constexpr std::array<unsigned char, 32> kNightlyPublicKey{
    0x24, 0xcb, 0xbb, 0xf5, 0x96, 0xb9, 0x77, 0x1c, 0x06, 0xef, 0x05, 0x36,
    0xeb, 0xd0, 0xc4, 0xcb, 0xcf, 0x5f, 0x8a, 0xd6, 0x67, 0xf9, 0x19, 0x2d,
    0xd0, 0x97, 0x14, 0x57, 0x40, 0x52, 0xe9, 0x02,
};

} // namespace trailer::Update
