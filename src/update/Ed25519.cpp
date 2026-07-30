#include "Ed25519.h"

#include <QRandomGenerator>

// TweetNaCl is plain C, public domain, vendored under third_party/. See
// third_party/tweetnacl/NOTICE.md for provenance. This translation unit
// is the only place in Trailer that includes it.
extern "C" {
#include "tweetnacl.h"
}

#include <cstring>
#include <vector>

// TweetNaCl declares `extern void randombytes(u8 *, u64)` but does not
// implement it; crypto_sign_ed25519_tweet_keypair() (test-only helper
// below) calls it, and because tweetnacl.c is one translation unit the
// linker needs this symbol defined even though the shipped app never
// calls the keypair function. Backed by the OS CSPRNG — this is
// intentionally the ONLY randomness-generating code in the update
// channel; verification itself is deterministic.
extern "C" void randombytes(unsigned char *buf, unsigned long long n) {
    QRandomGenerator *rng = QRandomGenerator::system();
    unsigned long long i = 0;
    for (; i + 4 <= n; i += 4) {
        const quint32 word = rng->generate();
        std::memcpy(buf + i, &word, 4);
    }
    if (i < n) {
        const quint32 word = rng->generate();
        std::memcpy(buf + i, &word, n - i);
    }
}

namespace trailer::Ed25519 {

bool verify(const QByteArray &message, const QByteArray &signature,
            const std::array<unsigned char, kPublicKeyBytes> &publicKey) {
    if (signature.size() != kSignatureBytes)
        return false;

    // crypto_sign_open expects a "signed message" = signature || message
    // and recovers `message` while checking the signature; there is no
    // detached-verify entry point in TweetNaCl, so we assemble that
    // layout here and hand the whole buffer to it. The output buffer is
    // sized the same as the input (TweetNaCl never writes more than it
    // read) and its used length comes back in `mlen`.
    std::vector<unsigned char> sm(static_cast<size_t>(signature.size() + message.size()));
    std::memcpy(sm.data(), signature.constData(), static_cast<size_t>(signature.size()));
    std::memcpy(sm.data() + signature.size(), message.constData(),
                static_cast<size_t>(message.size()));

    std::vector<unsigned char> out(sm.size());
    unsigned long long outLen = 0;
    const int rc = crypto_sign_ed25519_tweet_open(out.data(), &outLen, sm.data(), sm.size(),
                                                  publicKey.data());
    if (rc != 0)
        return false; // signature does not verify
    if (outLen != static_cast<unsigned long long>(message.size()))
        return false; // defensive: recovered message length must match
    return std::memcmp(out.data(), message.constData(), static_cast<size_t>(message.size())) == 0;
}

TestKeypair generateKeypairForTesting() {
    TestKeypair kp;
    crypto_sign_ed25519_tweet_keypair(kp.publicKey.data(), kp.secretKey.data());
    return kp;
}

QByteArray signForTesting(const QByteArray &message,
                          const std::array<unsigned char, 64> &secretKey) {
    std::vector<unsigned char> sm(static_cast<size_t>(message.size()) + kSignatureBytes);
    unsigned long long smLen = 0;
    crypto_sign_ed25519_tweet(sm.data(), &smLen,
                              reinterpret_cast<const unsigned char *>(message.constData()),
                              static_cast<unsigned long long>(message.size()), secretKey.data());
    // sm = signature(64) || message; the caller wants just the detached
    // signature.
    return QByteArray(reinterpret_cast<const char *>(sm.data()), kSignatureBytes);
}

} // namespace trailer::Ed25519
