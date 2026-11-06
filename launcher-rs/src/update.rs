//! Client core download (license-gated, checksum-verified, content-addressed
//! cache) and launcher self-update.

use std::io::Read;
use std::path::PathBuf;
use std::time::Duration;

use serde_json::Value;

use crate::config;
use crate::processes::Edition;
use crate::util;

#[derive(Default)]
pub struct CoreUpdate {
    pub ok: bool,
    pub path: Option<PathBuf>,
    pub sha: String,
    pub name: String,
    pub size: i64,
    pub error: String,
}

#[derive(Default)]
pub struct LauncherUpdate {
    pub updated: bool,
    pub message: String,
}

fn agent(timeout_ms: u64) -> ureq::Agent {
    ureq::AgentBuilder::new()
        .timeout(Duration::from_millis(timeout_ms))
        .user_agent(config::USER_AGENT)
        .build()
}

fn read_capped(reader: impl Read, limit: usize) -> Result<Vec<u8>, &'static str> {
    let mut body = Vec::new();
    let mut limited = reader.take(limit as u64 + 1);
    limited
        .read_to_end(&mut body)
        .map_err(|_| "read failed")?;
    if body.len() > limit {
        return Err("too large");
    }
    Ok(body)
}

fn cache_core(sha: &str, edition: Edition, bytes: &[u8]) -> Option<PathBuf> {
    let dir = util::update_dir()?;
    if !util::ensure_dir(&dir) {
        return None;
    }
    let path = dir.join(format!(
        "RuntimeHostCore-{}-{}.dll",
        edition.tag(),
        &sha[..12.min(sha.len())]
    ));
    if path.exists() {
        return Some(path);
    }
    if util::write_atomic(&path, bytes) {
        Some(path)
    } else {
        None
    }
}

/// Download the active core for the given edition. On failure the caller
/// falls back to `newest_cached_core`.
pub fn fetch_latest_core(license_key: &str, edition: Edition) -> CoreUpdate {
    let mut result = CoreUpdate::default();

    let key = crate::license::normalize_key(license_key);
    let fingerprint = crate::hwid::hwid();
    if key.is_empty() || fingerprint.is_empty() {
        result.error = "no license key".into();
        return result;
    }

    let url = format!(
        "https://{}{}?key={}&fp={}&edition={}",
        config::UPDATE_HOST,
        config::PATH_CORE_CURRENT,
        key,
        fingerprint,
        edition.tag()
    );

    let http = agent(config::UPDATE_TIMEOUT_MS);
    match http.get(&url).call() {
        Ok(response) => {
            let expected = response
                .header("X-DLL-SHA256")
                .unwrap_or_default()
                .to_ascii_lowercase();
            let server_name = response.header("X-DLL-NAME").unwrap_or_default().to_string();

            let body = match read_capped(response.into_reader(), config::MAX_CORE_BYTES) {
                Ok(body) => body,
                Err(reason) => {
                    result.error = format!("core download {reason}");
                    return result;
                }
            };
            if expected.is_empty() {
                result.error = "missing checksum header".into();
                return result;
            }
            if body.len() < 1024 {
                result.error = "empty core download".into();
                return result;
            }
            let actual = util::sha256_hex(&body);
            if actual != expected {
                result.error = "checksum mismatch".into();
                return result;
            }
            match cache_core(&actual, edition, &body) {
                Some(path) => {
                    result.ok = true;
                    result.sha = actual;
                    result.name = if server_name.is_empty() {
                        path.file_name()
                            .and_then(|n| n.to_str())
                            .unwrap_or_default()
                            .to_string()
                    } else {
                        server_name
                    };
                    result.size = body.len() as i64;
                    result.path = Some(path);
                }
                None => result.error = "cache write failed".into(),
            }
        }
        Err(ureq::Error::Status(403, _)) => {
            result.error = "server refused (license check)".into()
        }
        Err(ureq::Error::Status(404, _)) => result.error = "no core published yet".into(),
        Err(ureq::Error::Status(code, _)) => result.error = format!("http {code}"),
        Err(_) => result.error = "request failed".into(),
    }

    result
}

/// Newest cached core for an edition: (path, sha12) or (None, "").
pub fn newest_cached_core(edition: Edition) -> (Option<PathBuf>, String) {
    let Some(dir) = util::update_dir() else {
        return (None, String::new());
    };
    let prefix = format!("RuntimeHostCore-{}-", edition.tag());

    let Ok(entries) = std::fs::read_dir(&dir) else {
        return (None, String::new());
    };

    let mut best: Option<(PathBuf, String, std::time::SystemTime)> = None;
    for entry in entries.flatten() {
        let path = entry.path();
        let name = path
            .file_name()
            .and_then(|n| n.to_str())
            .map(str::to_string);
        let Some(name) = name else {
            continue;
        };
        if !name.starts_with(&prefix) || !name.ends_with(".dll") {
            continue;
        }
        let sha = &name[prefix.len()..name.len() - 4];
        let sha = sha.to_string();
        let Ok(modified) = entry.metadata().and_then(|m| m.modified()) else {
            continue;
        };
        let replace = match &best {
            Some((_, _, current)) => modified > *current,
            None => true,
        };
        if replace {
            best = Some((path, sha, modified));
        }
    }

    match best {
        Some((path, sha, _)) => (Some(path), sha),
        None => (None, String::new()),
    }
}

fn http_get_bytes(url: &str, limit: usize) -> Result<(u16, Vec<u8>), ()> {
    let http = agent(config::UPDATE_TIMEOUT_MS);
    match http.get(url).call() {
        Ok(response) => {
            let status = response.status();
            let body = read_capped(response.into_reader(), limit).map_err(|_| ())?;
            Ok((status, body))
        }
        Err(ureq::Error::Status(code, response)) => {
            let body = read_capped(response.into_reader(), limit).unwrap_or_default();
            Ok((code, body))
        }
        Err(_) => Err(()),
    }
}

/// Compares the running EXE against the published launcher and swaps in the
/// new build (rename old, move new in, roll back on failure).
pub fn check_and_apply_launcher_update() -> LauncherUpdate {
    let mut result = LauncherUpdate::default();

    if std::env::var("DRIP_NO_UPDATE").is_ok() {
        result.message = "update check disabled".into();
        return result;
    }
    if cfg!(debug_assertions) {
        result.message = "update check disabled (dev build)".into();
        return result;
    }

    let Ok(exe) = std::env::current_exe() else {
        result.message = "cannot resolve own path".into();
        return result;
    };

    // Development safety: never let a binary built under target\ replace
    // itself with the published launcher (it is a different implementation).
    // Release installs run from a normal folder, so this only affects dev.
    if std::env::var("DRIP_FORCE_UPDATE").is_err()
        && exe.to_string_lossy().to_lowercase().contains("\\target\\")
    {
        result.message = "update check disabled (dev path)".into();
        return result;
    }

    let info_url = format!("https://{}{}", config::UPDATE_HOST, config::PATH_LAUNCHER_INFO);
    let (status, info_body) = match http_get_bytes(&info_url, 64 * 1024) {
        Ok(result) => result,
        Err(()) => {
            result.message = "launcher info unavailable".into();
            return result;
        }
    };
    if status == 404 {
        result.message = "no launcher published".into();
        return result;
    }
    if status != 200 {
        result.message = format!("info http {status}");
        return result;
    }

    let Ok(info_json) = serde_json::from_slice::<Value>(&info_body) else {
        result.message = "bad info json".into();
        return result;
    };
    let Some(server_sha) = info_json.get("sha256").and_then(Value::as_str) else {
        result.message = "bad info json".into();
        return result;
    };
    let server_sha = server_sha.to_ascii_lowercase();

    let Some(own_sha) = util::sha256_file(&exe) else {
        result.message = "cannot hash own exe".into();
        return result;
    };
    if own_sha == server_sha {
        result.message = "launcher up to date".into();
        return result;
    }

    let exe_url = format!("https://{}{}", config::UPDATE_HOST, config::PATH_LAUNCHER_EXE);
    let (status, bytes) = match http_get_bytes(&exe_url, config::MAX_LAUNCHER_BYTES) {
        Ok(result) => result,
        Err(()) => {
            result.message = "launcher download failed".into();
            return result;
        }
    };
    if status != 200 || bytes.len() < 1024 {
        result.message = "launcher download failed".into();
        return result;
    }
    if util::sha256_hex(&bytes) != server_sha {
        result.message = "launcher checksum mismatch".into();
        return result;
    }

    let Some(dir) = util::update_dir() else {
        result.message = "no update cache dir".into();
        return result;
    };
    if !util::ensure_dir(&dir) {
        result.message = "no update cache dir".into();
        return result;
    }
    let staged = dir.join(format!(
        "RuntimeHost-{}.exe",
        &server_sha[..12.min(server_sha.len())]
    ));
    if !staged.exists() && !util::write_atomic(&staged, &bytes) {
        result.message = "stage write failed".into();
        return result;
    }

    if swap_executable(&exe, &staged) {
        result.updated = true;
        result.message = "launcher updated".into();
    } else {
        result.message = "swap failed".into();
    }
    result
}

fn swap_executable(running: &std::path::Path, staged: &std::path::Path) -> bool {
    use windows_sys::Win32::Foundation::BOOL;
    use windows_sys::Win32::Storage::FileSystem::{
        DeleteFileW, MoveFileExW, MOVEFILE_COPY_ALLOWED, MOVEFILE_REPLACE_EXISTING,
    };

    let mut old = running.as_os_str().to_os_string();
    old.push(".old");
    let old_path = PathBuf::from(old);

    let running_wide = util::wide_os(running.as_os_str());
    let old_wide = util::wide_os(old_path.as_os_str());
    let staged_wide = util::wide_os(staged.as_os_str());

    let flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED;

    unsafe {
        // A stale .old from an earlier update may be locked by another running
        // copy; ignore failures.
        DeleteFileW(old_wide.as_ptr());
        let moved_old: BOOL = MoveFileExW(running_wide.as_ptr(), old_wide.as_ptr(), flags);
        if moved_old == 0 {
            return false;
        }
        let moved_new: BOOL = MoveFileExW(staged_wide.as_ptr(), running_wide.as_ptr(), flags);
        if moved_new == 0 {
            let _ = MoveFileExW(old_wide.as_ptr(), running_wide.as_ptr(), flags);
            return false;
        }
    }
    true
}
