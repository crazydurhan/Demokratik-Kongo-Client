//! Client for the Drip licensing service. The wire format is frozen by
//! LISANS_VE_YUKLEME_STANDARTLARI.md; keep it byte-compatible with the C++
//! launcher and the panel.

use std::time::Duration;

use serde_json::{json, Value};

use crate::config;
use crate::store::{self, LicenseRecord};
use crate::token;
use crate::util;

#[derive(Clone)]
pub struct LicenseInfo {
    pub valid: bool,
    pub offline: bool,
    pub network_error: bool,
    pub code: String,
    pub detail: String,
    pub user_message: String,
    pub holder: String,
    pub license_id: String,
    pub expiry_iso: String,
    /// -1 = never expires
    pub days_left: i64,
    pub checked_at_unix: i64,
}

impl Default for LicenseInfo {
    fn default() -> Self {
        Self {
            valid: false,
            offline: false,
            network_error: false,
            code: String::new(),
            detail: String::new(),
            user_message: String::new(),
            holder: String::new(),
            license_id: String::new(),
            expiry_iso: String::new(),
            days_left: -1,
            checked_at_unix: 0,
        }
    }
}

/// Trim whitespace and ASCII-uppercase the input into the canonical key form.
pub fn normalize_key(raw: &str) -> String {
    let mut out = String::with_capacity(raw.len());
    for c in raw.chars() {
        if c.is_whitespace() {
            continue;
        }
        out.push(c.to_ascii_uppercase());
    }
    out
}

fn user_message_for_code(code: &str, detail: &str) -> String {
    match code {
        "VALID" => "License active".into(),
        "FINGERPRINT_SCOPE_MISMATCH" | "MACHINE_SCOPE_MISMATCH" | "TOO_MANY_MACHINES" => {
            "Key is linked to another PC - ask for a HWID reset".into()
        }
        "NO_MACHINE" => "Activation failed on this PC".into(),
        "SUSPENDED" | "BANNED" => "License suspended by the administrator".into(),
        "EXPIRED" => "License expired".into(),
        "NOT_FOUND" => "Invalid key - check it and try again".into(),
        "NETWORK" => "Could not reach the license server".into(),
        _ => {
            if detail.is_empty() {
                "License check failed".into()
            } else {
                detail.to_string()
            }
        }
    }
}

fn agent(timeout_ms: u64) -> ureq::Agent {
    ureq::AgentBuilder::new()
        .timeout(Duration::from_millis(timeout_ms))
        .user_agent(config::USER_AGENT)
        .build()
}

/// POST JSON; an HTTP error status is still a successful transport result.
fn post_json(
    agent: &ureq::Agent,
    host: &str,
    path: &str,
    body: &str,
    authorization: Option<&str>,
) -> Result<(u16, String), ()> {
    let url = format!("https://{host}{path}");
    let mut request = agent
        .post(&url)
        .set("Content-Type", "application/vnd.api+json")
        .set("Accept", "application/vnd.api+json");
    if let Some(auth) = authorization {
        request = request.set("Authorization", auth);
    }

    match request.send_string(body) {
        Ok(response) => Ok((response.status(), response.into_string().unwrap_or_default())),
        Err(ureq::Error::Status(code, response)) => {
            Ok((code, response.into_string().unwrap_or_default()))
        }
        Err(_) => Err(()),
    }
}

fn fetch_license_token(key: &str, fingerprint: &str) -> Option<String> {
    let body = json!({ "key": key, "fp": fingerprint }).to_string();
    let agent = agent(config::LICENSE_TIMEOUT_MS);
    let (status, text) = post_json(
        &agent,
        config::UPDATE_HOST,
        config::PATH_LICENSE_TOKEN,
        &body,
        None,
    )
    .ok()?;
    if status != 200 {
        return None;
    }
    let parsed: Value = serde_json::from_str(&text).ok()?;
    parsed
        .get("token")
        .and_then(Value::as_str)
        .map(str::to_string)
}

/// The server could not be reached: prefer a server-signed token (signature +
/// key + HWID + expiry), keep the 24h last-check grace as the fallback for
/// records written before tokens existed.
fn offline_fallback(mut info: LicenseInfo, key: &str) -> LicenseInfo {
    if let Some(record) = store::load_record() {
        if record.key == key {
            if !record.token.is_empty() {
                let verified = token::verify(&record.token);
                if verified.valid
                    && verified.key == key
                    && verified.fingerprint == crate::hwid::hwid()
                    && verified.expires_at > util::now_unix()
                {
                    info.valid = true;
                    info.offline = true;
                    info.holder = verified.holder;
                    info.expiry_iso = record.expiry.clone();
                    info.days_left = util::days_until_iso(&record.expiry);
                    let days = (verified.expires_at - util::now_unix()) / 86400;
                    info.user_message = format!("Offline token accepted ({days}d left)");
                    return info;
                }
            }

            if util::now_unix() - record.last_valid_at <= config::OFFLINE_GRACE_SECONDS {
                info.valid = true;
                info.offline = true;
                info.expiry_iso = record.expiry.clone();
                info.days_left = util::days_until_iso(&record.expiry);
                let hours = (util::now_unix() - record.last_valid_at) / 3600;
                info.user_message = format!("Offline mode - last check {hours}h ago");
                return info;
            }
        }
    }

    info.user_message = user_message_for_code("NETWORK", "");
    info
}

fn parse_meta(parsed: &Value) -> (String, String) {
    let meta = parsed.get("meta");
    let code = meta
        .and_then(|m| m.get("code"))
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_string();
    let detail = meta
        .and_then(|m| m.get("detail"))
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_string();
    (code, detail)
}

fn apply_data(info: &mut LicenseInfo, parsed: &Value) {
    let Some(data) = parsed.get("data").filter(|d| d.is_object()) else {
        return;
    };
    if let Some(id) = data.get("id").and_then(Value::as_str) {
        info.license_id = id.to_string();
    }
    if let Some(attributes) = data.get("attributes") {
        if let Some(name) = attributes.get("name").and_then(Value::as_str) {
            info.holder = name.to_string();
        }
        if let Some(expiry) = attributes.get("expiry").and_then(Value::as_str) {
            info.expiry_iso = expiry.to_string();
        }
    }
}

/// Validates the key for this machine and, on first use, binds it to the
/// hardware. Never panics; every failure is described by the result.
pub fn check(raw_key: &str) -> LicenseInfo {
    let mut info = LicenseInfo::default();
    info.checked_at_unix = util::now_unix();

    let key = normalize_key(raw_key);
    if key.is_empty() {
        info.code = "EMPTY".into();
        info.user_message = "Enter a license key".into();
        return info;
    }

    let fingerprint = crate::hwid::hwid();
    if fingerprint.is_empty() {
        info.code = "HWID".into();
        info.user_message = "Could not read a machine ID".into();
        return info;
    }

    let validate_body = json!({
        "meta": { "key": key, "scope": { "fingerprint": fingerprint } }
    })
    .to_string();

    let http = agent(config::LICENSE_TIMEOUT_MS);
    let (status, body) = match post_json(
        &http,
        config::API_HOST,
        config::PATH_VALIDATE_KEY,
        &validate_body,
        None,
    ) {
        Ok(result) => result,
        Err(()) => {
            info.network_error = true;
            info.code = "NETWORK".into();
            return offline_fallback(info, &key);
        }
    };
    let _ = status;

    let Ok(parsed) = serde_json::from_str::<Value>(&body) else {
        info.code = "PROTOCOL".into();
        info.user_message = "Unexpected server response".into();
        return info;
    };
    if parsed.get("meta").is_none() {
        info.code = "PROTOCOL".into();
        info.user_message = "Unexpected server response".into();
        return info;
    }

    let (mut code, mut detail) = parse_meta(&parsed);
    info.detail = detail.clone();
    apply_data(&mut info, &parsed);

    // First activation on this machine.
    if code == "NO_MACHINE" && !info.license_id.is_empty() {
        let machine_body = json!({
            "data": {
                "type": "machines",
                "attributes": {
                    "fingerprint": fingerprint,
                    "name": crate::hwid::computer_name(),
                    "platform": "windows"
                },
                "relationships": {
                    "license": { "data": { "type": "licenses", "id": info.license_id } }
                }
            }
        })
        .to_string();
        let authorization = format!("License {key}");

        let (activation_status, activation_body) = match post_json(
            &http,
            config::API_HOST,
            config::PATH_MACHINES,
            &machine_body,
            Some(&authorization),
        ) {
            Ok(result) => result,
            Err(()) => {
                info.network_error = true;
                info.code = "NETWORK".into();
                return offline_fallback(info, &key);
            }
        };

        if !(200..300).contains(&activation_status) {
            let mut detail = String::new();
            if let Ok(error_json) = serde_json::from_str::<Value>(&activation_body) {
                if let Some(first) = error_json
                    .get("errors")
                    .and_then(Value::as_array)
                    .and_then(|list| list.first())
                {
                    if let Some(code) = first.get("code").and_then(Value::as_str) {
                        detail.push_str(code);
                    }
                    if let Some(text) = first.get("detail").and_then(Value::as_str) {
                        if !detail.is_empty() {
                            detail.push_str(" - ");
                        }
                        detail.push_str(text);
                    }
                }
            }
            info.detail = detail.clone();
            info.code = if detail.contains("TOO_MANY_MACHINES") {
                "TOO_MANY_MACHINES".into()
            } else {
                "ACTIVATION_FAILED".into()
            };
            info.user_message = user_message_for_code(&info.code, &detail);
            return info;
        }

        // Re-validate now that the machine exists.
        let (_, recheck_body) = match post_json(
            &http,
            config::API_HOST,
            config::PATH_VALIDATE_KEY,
            &validate_body,
            None,
        ) {
            Ok(result) => result,
            Err(()) => {
                info.network_error = true;
                info.code = "NETWORK".into();
                return offline_fallback(info, &key);
            }
        };
        let Ok(rechecked) = serde_json::from_str::<Value>(&recheck_body) else {
            info.code = "PROTOCOL".into();
            info.user_message = "Unexpected server response".into();
            return info;
        };
        let (new_code, new_detail) = parse_meta(&rechecked);
        code = new_code;
        detail = new_detail;
        info.detail = detail.clone();
    }

    info.code = code.clone();
    info.valid = code == "VALID";

    if info.valid {
        info.days_left = if info.expiry_iso.is_empty() {
            -1
        } else {
            util::days_until_iso(&info.expiry_iso)
        };
        info.user_message = user_message_for_code("VALID", "");

        let mut token_text = fetch_license_token(&key, &fingerprint).unwrap_or_default();
        if token_text.is_empty() {
            if let Some(previous) = store::load_record() {
                if previous.key == key {
                    token_text = previous.token;
                }
            }
        }

        store::save_record(&LicenseRecord {
            key: key.clone(),
            last_valid_at: util::now_unix(),
            expiry: info.expiry_iso.clone(),
            token: token_text,
        });
    } else {
        info.user_message = user_message_for_code(&code, &detail);
    }

    info
}
