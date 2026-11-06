//! Small shared helpers: UTF-16, hashing, time, files, console output.

use std::ffi::OsStr;
use std::os::windows::ffi::OsStrExt;
use std::path::{Path, PathBuf};

use base64::engine::general_purpose::{STANDARD, URL_SAFE, URL_SAFE_NO_PAD};
use base64::Engine;
use sha2::{Digest, Sha256};

/// NUL-terminated UTF-16 for Win32 calls.
pub fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// NUL-terminated UTF-16 for a path or any OsStr.
pub fn wide_os(s: &OsStr) -> Vec<u16> {
    s.encode_wide().chain(std::iter::once(0)).collect()
}

/// UTF-16 (NUL-terminated or fixed buffer) -> String.
pub fn from_wide(buf: &[u16]) -> String {
    let end = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    String::from_utf16_lossy(&buf[..end])
}

/// NUL-terminated UTF-16 path buffer -> PathBuf.
#[allow(dead_code)]
pub fn from_wide_path(buf: &[u16]) -> PathBuf {
    PathBuf::from(from_wide(buf))
}

/// %APPDATA%\RuntimeHost (created on demand by the callers that write there).
pub fn appdata_dir() -> Option<PathBuf> {
    let appdata = std::env::var_os("APPDATA")?;
    Some(PathBuf::from(appdata).join(crate::config::APP_FOLDER))
}

/// %APPDATA%\RuntimeHost\update
pub fn update_dir() -> Option<PathBuf> {
    Some(appdata_dir()?.join("update"))
}

/// %APPDATA%\RuntimeHost\license.dat
pub fn record_path() -> Option<PathBuf> {
    Some(appdata_dir()?.join(crate::config::RECORD_FILE))
}

/// %APPDATA%\RuntimeHost\launcher.log
pub fn log_path() -> Option<PathBuf> {
    Some(appdata_dir()?.join(crate::config::LOG_FILE))
}

pub fn ensure_dir(path: &Path) -> bool {
    std::fs::create_dir_all(path).is_ok()
}

/// Lowercase hex SHA-256.
pub fn sha256_hex(data: &[u8]) -> String {
    let digest = Sha256::digest(data);
    let mut out = String::with_capacity(64);
    for byte in digest {
        out.push_str(&format!("{:02x}", byte));
    }
    out
}

pub fn sha256_file(path: &Path) -> Option<String> {
    let bytes = std::fs::read(path).ok()?;
    if bytes.is_empty() {
        return None;
    }
    Some(sha256_hex(&bytes))
}

/// FNV-1a 64-bit; used for content-addressed cache folder names.
pub fn fnv1a64(data: &[u8]) -> u64 {
    let mut hash: u64 = 0xcbf29ce484222325;
    for byte in data {
        hash ^= *byte as u64;
        hash = hash.wrapping_mul(0x100000001b3);
    }
    hash
}

pub fn now_unix() -> i64 {
    chrono::Utc::now().timestamp()
}

#[allow(dead_code)]
pub fn iso_now() -> String {
    chrono::Utc::now().to_rfc3339_opts(chrono::SecondsFormat::Millis, true)
}

/// Whole days until an ISO-8601 timestamp, rounded up; -1 when unparsable.
pub fn days_until_iso(iso: &str) -> i64 {
    if iso.is_empty() {
        return -1;
    }
    let Ok(when) = chrono::DateTime::parse_from_rfc3339(iso) else {
        return -1;
    };
    let seconds = when.timestamp() - now_unix();
    (seconds as f64 / 86400.0).ceil() as i64
}

pub fn lower_ascii(s: &str) -> String {
    s.chars().map(|c| c.to_ascii_lowercase()).collect()
}

pub fn b64_decode_standard(s: &str) -> Option<Vec<u8>> {
    STANDARD.decode(s.trim()).ok()
}

/// base64url decoder that accepts both padded and unpadded input.
pub fn b64_decode_url(s: &str) -> Option<Vec<u8>> {
    if let Ok(bytes) = URL_SAFE_NO_PAD.decode(s.trim()) {
        return Some(bytes);
    }
    URL_SAFE.decode(s.trim()).ok()
}

pub fn hex_decode(s: &str) -> Option<Vec<u8>> {
    if s.len() % 2 != 0 {
        return None;
    }
    let mut out = Vec::with_capacity(s.len() / 2);
    let bytes = s.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        let hi = (bytes[i] as char).to_digit(16)?;
        let lo = (bytes[i + 1] as char).to_digit(16)?;
        out.push(((hi << 4) | lo) as u8);
        i += 2;
    }
    Some(out)
}

/// Write a file through a temporary sibling + rename (updaters may see it).
pub fn write_atomic(path: &Path, data: &[u8]) -> bool {
    if let Some(parent) = path.parent() {
        if !parent.as_os_str().is_empty() && !ensure_dir(parent) {
            return false;
        }
    }
    let tmp = path.with_extension(format!(
        "{}tmp",
        path.extension().and_then(|e| e.to_str()).map(|e| format!("{e}.")).unwrap_or_default()
    ));
    if std::fs::write(&tmp, data).is_err() {
        let _ = std::fs::remove_file(&tmp);
        return false;
    }
    if std::fs::rename(&tmp, path).is_err() {
        let _ = std::fs::remove_file(&tmp);
        return false;
    }
    true
}

/// Attach to the parent console so a GUI-subsystem binary can print for the
/// hidden CLI (same behaviour as the C++ launcher's AttachConsole call).
pub fn attach_console() {
    use windows_sys::Win32::Foundation::INVALID_HANDLE_VALUE;
    use windows_sys::Win32::Storage::FileSystem::{
        CreateFileW, FILE_SHARE_READ, FILE_SHARE_WRITE, OPEN_EXISTING,
    };
    use windows_sys::Win32::System::Console::{
        AttachConsole, SetStdHandle, ATTACH_PARENT_PROCESS, STD_ERROR_HANDLE, STD_OUTPUT_HANDLE,
    };

    unsafe {
        if AttachConsole(ATTACH_PARENT_PROCESS) == 0 {
            return;
        }
        for (name, handle) in [
            ("CONOUT$\0", STD_OUTPUT_HANDLE),
            ("CONOUT$\0", STD_ERROR_HANDLE),
        ] {
            let wide_name: Vec<u16> = name.encode_utf16().collect();
            let file = CreateFileW(
                wide_name.as_ptr(),
                0x4000_0000, // GENERIC_WRITE
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                std::ptr::null(),
                OPEN_EXISTING,
                0,
                std::ptr::null_mut(),
            );
            if file != INVALID_HANDLE_VALUE {
                SetStdHandle(handle, file);
            }
        }
    }
}

/// CLI stdout (no-op when no console could be attached).
pub fn console_out(line: &str) {
    use windows_sys::Win32::Foundation::INVALID_HANDLE_VALUE;
    use windows_sys::Win32::Storage::FileSystem::WriteFile;
    use windows_sys::Win32::System::Console::GetStdHandle;
    use windows_sys::Win32::System::Console::STD_OUTPUT_HANDLE;

    unsafe {
        let handle = GetStdHandle(STD_OUTPUT_HANDLE);
        if handle == INVALID_HANDLE_VALUE || handle.is_null() {
            return;
        }
        let mut text = line.to_string();
        if !text.ends_with('\n') {
            text.push('\n');
        }
        let mut written = 0u32;
        WriteFile(
            handle,
            text.as_ptr(),
            text.len() as u32,
            &mut written,
            std::ptr::null_mut(),
        );
    }
}

pub fn console_err(line: &str) {
    use windows_sys::Win32::Foundation::INVALID_HANDLE_VALUE;
    use windows_sys::Win32::Storage::FileSystem::WriteFile;
    use windows_sys::Win32::System::Console::GetStdHandle;
    use windows_sys::Win32::System::Console::STD_ERROR_HANDLE;

    unsafe {
        let handle = GetStdHandle(STD_ERROR_HANDLE);
        if handle == INVALID_HANDLE_VALUE || handle.is_null() {
            return;
        }
        let mut text = line.to_string();
        if !text.ends_with('\n') {
            text.push('\n');
        }
        let mut written = 0u32;
        WriteFile(
            handle,
            text.as_ptr(),
            text.len() as u32,
            &mut written,
            std::ptr::null_mut(),
        );
    }
}
