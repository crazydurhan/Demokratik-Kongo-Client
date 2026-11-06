//! Drip Launcher (Rust). GUI by default; hidden CLI for smoke tests:
//!   RuntimeHost.exe --inject <pid> [dll] [--edition modern|legacy]
//!   RuntimeHost.exe --token-test <file>

#![windows_subsystem = "windows"]

mod config;
mod gui;
mod hwid;
mod inject;
mod license;
mod log;
mod processes;
mod store;
mod token;
mod update;
mod util;

use std::ffi::OsString;
use std::path::PathBuf;

use processes::Edition;

fn flag_value<'a>(args: &'a [OsString], flag: &str) -> Option<&'a OsString> {
    let index = args
        .iter()
        .position(|arg| arg.to_str() == Some(flag))?;
    args.get(index + 1)
}

fn has_flag(args: &[OsString], flag: &str) -> bool {
    args.iter().any(|arg| arg.to_str() == Some(flag))
}

fn edition_from_args(args: &[OsString]) -> Edition {
    match flag_value(args, "--edition").and_then(|value| value.to_str()) {
        Some("modern") => Edition::Modern,
        _ => Edition::Legacy,
    }
}

/// `--token-test <file>`: exit 0 valid, 2 invalid/expired, 3 unreadable.
fn run_token_test(args: &[OsString]) -> Option<i32> {
    let path = flag_value(args, "--token-test")?;
    util::attach_console();

    let Ok(text) = std::fs::read_to_string(path) else {
        util::console_err(&format!("[dk-token] cannot open {}", path.to_string_lossy()));
        return Some(3);
    };
    let token = text.trim();

    let info = token::verify(token);
    if !info.valid {
        util::console_err(&format!("[dk-token] INVALID: {}", info.reason));
        return Some(2);
    }

    let now = util::now_unix();
    if info.expires_at <= now {
        util::console_err("[dk-token] EXPIRED");
        return Some(2);
    }

    util::console_out(&format!(
        "[dk-token] VALID key={} fp={} name={} days_left={}",
        info.key,
        &info.fingerprint[..16.min(info.fingerprint.len())],
        info.holder,
        (info.expires_at - now) / 86400
    ));
    Some(0)
}

/// `--selftest`: prints the machine fingerprint and checks the DPAPI record
/// round-trip without touching the user's stored licence.
fn run_selftest(args: &[OsString]) -> Option<i32> {
    if !has_flag(args, "--selftest") {
        return None;
    }
    util::attach_console();
    log::init();

    let fingerprint = hwid::hwid();
    util::console_out(&format!("[dk-selftest] hwid={fingerprint}"));
    log::info(&format!("selftest hwid={fingerprint}"));

    let backup = store::load_record();
    let probe = store::LicenseRecord {
        key: "DKK-SELFT-EST00-00000-00000".into(),
        last_valid_at: util::now_unix(),
        expiry: String::new(),
        token: String::new(),
    };
    let saved = store::save_record(&probe);
    let roundtrip = store::load_record()
        .map(|record| record.key == probe.key)
        .unwrap_or(false);

    match &backup {
        Some(record) => {
            let _ = store::save_record(record);
        }
        None => store::clear_record(),
    }

    let path = util::record_path()
        .map(|p| p.display().to_string())
        .unwrap_or_default();
    util::console_out(&format!(
        "[dk-selftest] dpapi_save={saved} roundtrip={roundtrip} record={path}"
    ));
    log::info(&format!(
        "selftest dpapi_save={saved} roundtrip={roundtrip} record={path}"
    ));
    Some(if saved && roundtrip { 0 } else { 1 })
}

/// `--license-set <key>` / `--license-clear`: QA/support helpers for the
/// DPAPI record; never used by the normal flow.
fn run_license_record_tools(args: &[OsString]) -> Option<i32> {
    if has_flag(args, "--license-set") {
        let key = flag_value(args, "--license-set")?.to_string_lossy().to_string();
        util::attach_console();
        let normalized = license::normalize_key(&key);
        let saved = store::save_record(&store::LicenseRecord {
            key: normalized,
            last_valid_at: util::now_unix() - 3600,
            expiry: String::new(),
            token: String::new(),
        });
        util::console_out(&format!("[dk-license] saved={saved}"));
        return Some(if saved { 0 } else { 1 });
    }
    if has_flag(args, "--license-clear") {
        util::attach_console();
        store::clear_record();
        util::console_out("[dk-license] cleared");
        return Some(0);
    }
    None
}

/// `--core-fetch <key> [--edition modern|legacy]`: QA helper that exercises
/// the licence-gated core download + cache path without the GUI.
fn run_core_fetch(args: &[OsString]) -> Option<i32> {
    let key = flag_value(args, "--core-fetch")?.to_string_lossy().to_string();
    util::attach_console();
    log::init();

    let edition = edition_from_args(args);
    let result = update::fetch_latest_core(&key, edition);
    util::console_out(&format!(
        "[dk-core] ok={} sha={} name={} size={} path={} error={}",
        result.ok,
        result.sha,
        result.name,
        result.size,
        result
            .path
            .as_ref()
            .map(|path| path.display().to_string())
            .unwrap_or_default(),
        result.error
    ));
    Some(if result.ok { 0 } else { 1 })
}

/// `--inject <pid> [dll]`: runs the java-hosted injector without the GUI.
fn run_cli_inject(args: &[OsString]) -> Option<i32> {
    let position = args
        .iter()
        .position(|arg| arg.to_str() == Some("--inject"))?;
    util::attach_console();

    let pid: u32 = args
        .get(position + 1)
        .and_then(|value| value.to_str())
        .and_then(|value| value.parse().ok())
        .unwrap_or(0);
    let dll_argument = args
        .get(position + 2)
        .filter(|value| !value.to_string_lossy().starts_with('-'))
        .map(PathBuf::from);
    let edition = edition_from_args(args);

    if pid == 0 {
        util::console_err(
            "[dk-inject] usage: RuntimeHost.exe --inject <pid> [dll] [--edition modern|legacy]",
        );
        return Some(3);
    }

    let payload = match dll_argument {
        Some(path) => path,
        None => update::newest_cached_core(edition).0.unwrap_or_default(),
    };
    if payload.as_os_str().is_empty() || !payload.is_file() {
        util::console_err("[dk-inject] no payload DLL (pass one, or cache a core first)");
        return Some(4);
    }

    let Some(agent) = inject::ensure_agent_dll() else {
        util::console_err("[dk-inject] embedded injector DLL unavailable");
        return Some(4);
    };

    let candidates = processes::collect_java_candidates(pid);
    let Some(java_exe) = candidates.first() else {
        util::console_err("[dk-inject] no java.exe found to host the injector");
        return Some(4);
    };

    util::console_out(&format!(
        "[dk-inject] target={} payload={}",
        pid,
        payload.display()
    ));
    util::console_out(&format!("[dk-inject] java host={}", java_exe.display()));

    match inject::run_java_agent(java_exe, &agent, pid, &payload) {
        Ok(detail) => {
            util::console_out(&format!("[dk-inject] success - {detail}"));
            Some(0)
        }
        Err(reason) => {
            util::console_err(&format!("[dk-inject] FAILED: {reason}"));
            Some(5)
        }
    }
}

fn main() {
    let args: Vec<OsString> = std::env::args_os().collect();

    if has_flag(&args, "--token-test") {
        std::process::exit(run_token_test(&args).unwrap_or(3));
    }

    if has_flag(&args, "--selftest") {
        std::process::exit(run_selftest(&args).unwrap_or(1));
    }

    if has_flag(&args, "--license-set") || has_flag(&args, "--license-clear") {
        std::process::exit(run_license_record_tools(&args).unwrap_or(1));
    }

    if has_flag(&args, "--core-fetch") {
        std::process::exit(run_core_fetch(&args).unwrap_or(1));
    }

    if has_flag(&args, "--inject") {
        std::process::exit(run_cli_inject(&args).unwrap_or(3));
    }

    log::init();
    log::info(&format!("Drip Launcher v{} (rust)", config::VERSION));

    let _ = hwid::hwid();
    std::process::exit(gui::run());
}
