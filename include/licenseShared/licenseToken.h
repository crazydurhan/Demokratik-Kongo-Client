#pragma once

#include <cstdint>
#include <string>

namespace dk::licensing {

// Phase-3 offline licence token: "<base64url(payload)>.<base64url(signature)>"
// signed by the licence service with Ed25519. The payload is JSON:
//   { "v": 1, "key": "DKK-...", "fp": "<hwid hex>", "name": "...",
//     "iat": <unix>, "exp": <unix> }
// The service only signs tokens after a full server-side validation with the
// machine fingerprint, so a valid token means "this licence was active on
// this machine as of iat, and is covered until exp".
struct TokenInfo
{
    bool valid = false;      // signature + structure OK (policy is the caller's job)
    std::string reason;      // why invalid, for logs / status lines
    std::string key;         // licence key the token was issued for
    std::string fingerprint; // HWID the token was issued for
    std::string holder;      // display name (may be empty)
    int64_t issuedAt = 0;
    int64_t expiresAt = 0;
};

// Verifies the token against the embedded service public key and parses its
// payload. Never throws. Callers additionally check key match, fingerprint
// match and expiry against the local clock (see licenseToken.cpp header note
// in the standards document, section 7.1).
TokenInfo verifyLicenseToken(const std::string& token);

} // namespace dk::licensing
