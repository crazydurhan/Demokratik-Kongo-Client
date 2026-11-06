#pragma once

namespace dk::licensing {

// Raw 32-byte Ed25519 public key of the Drip Launcher licence service,
// as lowercase hex. The private half lives ONLY on the VPS (inside the
// dkk-update container at /data/ed25519.key.pem); clients can verify but
// never forge tokens - see LISANS_VE_YUKLEME_STANDARTLARI.md.
//
// Rotating the keypair requires rebuilding BOTH the launcher and the core
// with the new hex (and re-issuing tokens; old tokens stop verifying).
inline constexpr char kServicePublicKeyHex[] =
    "82f710ba58ab744d4aead5b77c096154a4091ff0439f2de422f023b939a2b8b1";

} // namespace dk::licensing
