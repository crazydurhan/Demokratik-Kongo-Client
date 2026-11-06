//! Offline licence token verification: base64url(payload).base64url(signature)
//! signed with the service's Ed25519 key. Verification-only; the private half
//! never leaves the server.

use ed25519_dalek::{Signature, Verifier, VerifyingKey};
use serde_json::Value;

use crate::config;
use crate::util;

#[derive(Clone, Default)]
pub struct TokenInfo {
    pub valid: bool,
    pub reason: String,
    pub key: String,
    pub fingerprint: String,
    pub holder: String,
    pub issued_at: i64,
    pub expires_at: i64,
}

pub fn verify(token: &str) -> TokenInfo {
    let mut info = TokenInfo::default();

    let Some(dot) = token.find('.') else {
        info.reason = "malformed token".into();
        return info;
    };
    if dot == 0 || dot + 1 >= token.len() {
        info.reason = "malformed token".into();
        return info;
    }

    let Some(payload) = util::b64_decode_url(&token[..dot]) else {
        info.reason = "payload decode failed".into();
        return info;
    };
    if payload.is_empty() || payload.len() > 4096 {
        info.reason = "payload decode failed".into();
        return info;
    }

    let Some(signature) = util::b64_decode_url(&token[dot + 1..]) else {
        info.reason = "signature decode failed".into();
        return info;
    };
    if signature.len() != 64 {
        info.reason = "signature decode failed".into();
        return info;
    }

    let Some(public_key) = util::hex_decode(config::LICENSE_PUBLIC_KEY_HEX) else {
        info.reason = "embedded public key invalid".into();
        return info;
    };
    if public_key.len() != 32 {
        info.reason = "embedded public key invalid".into();
        return info;
    }
    let mut pk = [0u8; 32];
    pk.copy_from_slice(&public_key);

    let Ok(verifying_key) = VerifyingKey::from_bytes(&pk) else {
        info.reason = "embedded public key invalid".into();
        return info;
    };

    let mut sig = [0u8; 64];
    sig.copy_from_slice(&signature);
    let signature = Signature::from_bytes(&sig);

    if verifying_key.verify(&payload, &signature).is_err() {
        info.reason = "signature verification failed".into();
        return info;
    }

    let Ok(parsed) = serde_json::from_slice::<Value>(&payload) else {
        info.reason = "payload is not json".into();
        return info;
    };
    if parsed.get("v").and_then(Value::as_i64) != Some(1) {
        info.reason = "unsupported token version".into();
        return info;
    }

    let text = |field: &str| {
        parsed
            .get(field)
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_string()
    };
    info.key = text("key");
    info.fingerprint = text("fp");
    info.holder = text("name");
    info.issued_at = parsed.get("iat").and_then(Value::as_i64).unwrap_or(0);
    info.expires_at = parsed.get("exp").and_then(Value::as_i64).unwrap_or(0);

    if info.key.is_empty() || info.fingerprint.is_empty() || info.expires_at <= 0 {
        info.reason = "token fields missing".into();
        return info;
    }

    info.valid = true;
    info
}
