#include "licenseToken.h"

#include "licensePublicKey.h"

// The vendored tweetnacl.h is a plain C header without language-linkage
// guards; wrapping it keeps the C++ call sites resolving to the C symbols
// that tweetnacl.c actually exports (otherwise MSVC mangles the names).
extern "C" {
#include "tweetnacl.h"
}

#include <cstring>
#include <vector>

#include "json.hpp"

/*
================================================================================
    Demokratik Kongo - offline licence token verification (shared)
--------------------------------------------------------------------------------
    Compiled into BOTH the launcher and the core so a token is verified with
    the exact same code on both sides. The algorithm:

      1. split "<b64url payload>.<b64url signature>"
      2. Ed25519-verify (TweetNaCl, public domain) against the embedded key
      3. parse the payload JSON and sanity-check the required fields

    Tech note: TweetNaCl's crypto_sign_open expects the signed message in
    its own layout - the 64-byte signature FIRST, then the message (see
    crypto_sign's `sm[64 + i] = m[i]`) - so the token is re-assembled into
    that shape before verification.
================================================================================
*/

namespace dk::licensing {

namespace {

// tweetnacl.c declares `randombytes` as an external hook and never defines
// it. Token verification never needs randomness; this stub only satisfies
// the linker when the TweetNaCl object is pulled in. (The u8/u64 aliases are
// private to tweetnacl.c, so the raw types are spelled out here.)
extern "C" void randombytes(unsigned char* x, unsigned long long l)
{
    (void)x;
    (void)l;
}

int hexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hexToBytes(const char* hex, size_t len, std::vector<unsigned char>& out)
{
    if (len == 0 || len % 2 != 0)
        return false;
    out.clear();
    out.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        const int hi = hexNibble(hex[i]);
        const int lo = hexNibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return true;
}

bool base64UrlDecode(const std::string& in, std::vector<unsigned char>& out)
{
    static const char kAlpha[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int map[256];
    memset(map, -1, sizeof(map));
    for (int i = 0; i < 64; ++i)
        map[static_cast<unsigned char>(kAlpha[i])] = i;

    out.clear();
    int acc = 0, bits = 0;
    for (char ch : in) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c == '=' || c == '\n' || c == '\r')
            continue;
        if (c == '-') c = '+';
        if (c == '_') c = '/';
        const int v = map[c];
        if (v < 0)
            return false;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

} // namespace

TokenInfo verifyLicenseToken(const std::string& token)
{
    TokenInfo info;

    const size_t dot = token.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= token.size()) {
        info.reason = "malformed token";
        return info;
    }

    std::vector<unsigned char> payload, sig;
    if (!base64UrlDecode(token.substr(0, dot), payload) || payload.empty() || payload.size() > 4096) {
        info.reason = "payload decode failed";
        return info;
    }
    if (!base64UrlDecode(token.substr(dot + 1), sig) || sig.size() != 64) {
        info.reason = "signature decode failed";
        return info;
    }

    std::vector<unsigned char> pk;
    if (!hexToBytes(kServicePublicKeyHex, sizeof(kServicePublicKeyHex) - 1, pk) || pk.size() != 32) {
        info.reason = "embedded public key invalid";
        return info;
    }

    // TweetNaCl's signed-message layout is "signature || message" (its
    // crypto_sign writes `sm[64 + i] = m[i]`); assemble exactly that.
    std::vector<unsigned char> sm(payload.size() + sig.size());
    memcpy(sm.data(), sig.data(), sig.size());
    memcpy(sm.data() + sig.size(), payload.data(), payload.size());

    std::vector<unsigned char> opened(sm.size());
    unsigned long long openedLen = 0;
    if (crypto_sign_open(opened.data(), &openedLen, sm.data(), sm.size(), pk.data()) != 0) {
        info.reason = "signature verification failed";
        return info;
    }

    const nlohmann::json j = nlohmann::json::parse(payload.begin(), payload.end(), nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        info.reason = "payload is not json";
        return info;
    }
    if (!j.contains("v") || !j["v"].is_number_integer() || j["v"].get<int>() != 1) {
        info.reason = "unsupported token version";
        return info;
    }
    if (j.contains("key") && j["key"].is_string())
        info.key = j["key"].get<std::string>();
    if (j.contains("fp") && j["fp"].is_string())
        info.fingerprint = j["fp"].get<std::string>();
    if (j.contains("name") && j["name"].is_string())
        info.holder = j["name"].get<std::string>();
    if (j.contains("iat") && j["iat"].is_number_integer())
        info.issuedAt = j["iat"].get<int64_t>();
    if (j.contains("exp") && j["exp"].is_number_integer())
        info.expiresAt = j["exp"].get<int64_t>();

    if (info.key.empty() || info.fingerprint.empty() || info.expiresAt <= 0) {
        info.reason = "token fields missing";
        return info;
    }

    info.valid = true;
    return info;
}

} // namespace dk::licensing
