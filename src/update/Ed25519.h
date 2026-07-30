#pragma once

#include <QByteArray>

#include <array>

namespace trailer::Ed25519 {

// ed25519 signature verification, backed by the vendored TweetNaCl
// reference implementation (third_party/tweetnacl — see NOTICE.md
// there). This is the ONLY place TweetNaCl's crypto_sign_open is
// called from shipped code.
//
// Trailer's update channel signs a *detached* signature over a message
// (the appcast feed's payload bytes): `signature` is the raw 64-byte
// ed25519 signature, `publicKey` is the 32-byte ed25519 public key
// embedded in the binary (see UpdatePublicKey.h). Returns true only if
// the signature verifies against exactly this message and this key —
// any mismatch (wrong key, tampered message, truncated/garbled
// signature) returns false. This function does no I/O and trusts
// nothing about its inputs' provenance; the caller (UpdateFeedParser)
// must call it BEFORE reading any field out of the message it protects.
constexpr int kPublicKeyBytes = 32;
constexpr int kSignatureBytes = 64;

bool verify(const QByteArray &message, const QByteArray &signature,
            const std::array<unsigned char, kPublicKeyBytes> &publicKey);

// ---------------------------------------------------------------------
// Test-only helpers below. The shipped app never generates a keypair or
// signs anything — Trailer only ever *verifies*. These exist so unit
// tests can construct signed fixtures (a tampered-feed test needs a
// real signature to tamper with) without a second crypto dependency.
// Do not call these from application code; nothing outside
// tests/test_update_*.cpp should link against them.
// ---------------------------------------------------------------------

struct TestKeypair {
    std::array<unsigned char, kPublicKeyBytes> publicKey{};
    std::array<unsigned char, 64> secretKey{}; // ed25519 secret key (seed || pubkey), TweetNaCl format
};

// Generates a fresh keypair using the OS CSPRNG. Test-only — see above.
TestKeypair generateKeypairForTesting();

// Produces a detached 64-byte ed25519 signature over `message` using
// `secretKey`. Test-only — see above.
QByteArray signForTesting(const QByteArray &message,
                          const std::array<unsigned char, 64> &secretKey);

} // namespace trailer::Ed25519
