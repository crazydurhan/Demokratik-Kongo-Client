//! DPAPI-encrypted licence record (%APPDATA%\RuntimeHost\license.dat).
//! Byte-compatible with the C++ launcher so an existing installation keeps
//! working after the Rust launcher replaces it.

use serde::{Deserialize, Serialize};
use windows_sys::Win32::Foundation::{LocalFree, HLOCAL};
use windows_sys::Win32::Security::Cryptography::{
    CryptProtectData, CryptUnprotectData, CRYPT_INTEGER_BLOB, CRYPTPROTECT_UI_FORBIDDEN,
};

use crate::util::{self, wide};

#[derive(Clone, Default, Serialize, Deserialize)]
pub struct LicenseRecord {
    #[serde(default)]
    pub key: String,
    #[serde(rename = "lastValidAt", default)]
    pub last_valid_at: i64,
    #[serde(default)]
    pub expiry: String,
    #[serde(default)]
    pub token: String,
}

fn blob_from(bytes: &[u8]) -> CRYPT_INTEGER_BLOB {
    CRYPT_INTEGER_BLOB {
        cbData: bytes.len() as u32,
        pbData: bytes.as_ptr() as *mut u8,
    }
}

fn take_blob(blob: CRYPT_INTEGER_BLOB) -> Vec<u8> {
    if blob.pbData.is_null() || blob.cbData == 0 {
        return Vec::new();
    }
    let bytes = unsafe { std::slice::from_raw_parts(blob.pbData, blob.cbData as usize).to_vec() };
    unsafe {
        LocalFree(blob.pbData as HLOCAL);
    }
    bytes
}

fn protect(plain: &[u8]) -> Option<Vec<u8>> {
    let description = wide("RuntimeHost license");
    let input = blob_from(plain);
    let mut output = CRYPT_INTEGER_BLOB {
        cbData: 0,
        pbData: std::ptr::null_mut(),
    };

    let ok = unsafe {
        CryptProtectData(
            &input,
            description.as_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
            CRYPTPROTECT_UI_FORBIDDEN,
            &mut output,
        )
    };
    if ok == 0 {
        return None;
    }
    let bytes = take_blob(output);
    if bytes.is_empty() {
        None
    } else {
        Some(bytes)
    }
}

fn unprotect(cipher: &[u8]) -> Option<Vec<u8>> {
    let input = blob_from(cipher);
    let mut output = CRYPT_INTEGER_BLOB {
        cbData: 0,
        pbData: std::ptr::null_mut(),
    };

    let ok = unsafe {
        CryptUnprotectData(
            &input,
            std::ptr::null_mut(),
            std::ptr::null(),
            std::ptr::null(),
            std::ptr::null(),
            CRYPTPROTECT_UI_FORBIDDEN,
            &mut output,
        )
    };
    if ok == 0 {
        return None;
    }
    let bytes = take_blob(output);
    if bytes.is_empty() {
        None
    } else {
        Some(bytes)
    }
}

pub fn load_record() -> Option<LicenseRecord> {
    let path = util::record_path()?;
    let blob = std::fs::read(path).ok()?;
    if blob.is_empty() {
        return None;
    }
    let plain = unprotect(&blob)?;
    let record: LicenseRecord = serde_json::from_slice(&plain).ok()?;
    if record.key.is_empty() {
        None
    } else {
        Some(record)
    }
}

pub fn save_record(record: &LicenseRecord) -> bool {
    let Some(path) = util::record_path() else {
        return false;
    };
    if let Some(parent) = path.parent() {
        if !util::ensure_dir(parent) {
            return false;
        }
    }
    let Ok(plain) = serde_json::to_vec(record) else {
        return false;
    };
    match protect(&plain) {
        Some(cipher) => util::write_atomic(&path, &cipher),
        None => false,
    }
}

pub fn clear_record() {
    if let Some(path) = util::record_path() {
        let _ = std::fs::remove_file(path);
    }
}
