//! Frozen contract with the licensing / update service and the launcher's
//! on-disk layout. These values mirror
//! `Drip-Launcher/include/serviceEndpoints.h` and
//! `Drip-Launcher/include/stealthNames.h`; keep them in sync.

/// Launcher version (shown in the window and the log).
pub const VERSION: &str = "1.1.0";

/// System-facing window title kept generic so third-party launchers do not
/// blacklist the process surface.
pub const WINDOW_TITLE: &str = "Host Process for Windows Services";

/// %APPDATA% subfolder, temp cache folder and file names.
pub const APP_FOLDER: &str = "RuntimeHost";
#[allow(dead_code)]
pub const LAUNCHER_EXE: &str = "RuntimeHost.exe";
pub const INJECTOR_DLL: &str = "RuntimeHostInjector.dll";
pub const RECORD_FILE: &str = "license.dat";
pub const LOG_FILE: &str = "launcher.log";

/// HTTP user agent (same string the C++ launcher sends).
pub const USER_AGENT: &str = "RuntimeHostLauncher/1.0";

// ------------------------------------------------------------------ service
pub const API_HOST: &str = "dripclient.duckdns.org";
pub const UPDATE_HOST: &str = "dripclient.duckdns.org";

pub const PATH_VALIDATE_KEY: &str = "/v1/accounts/drip/licenses/actions/validate-key";
pub const PATH_MACHINES: &str = "/v1/accounts/drip/machines";
pub const PATH_LICENSE_TOKEN: &str = "/up/api/token";
pub const PATH_CORE_CURRENT: &str = "/up/dl/current";
pub const PATH_LAUNCHER_INFO: &str = "/up/dl/launcher/info";
pub const PATH_LAUNCHER_EXE: &str = "/up/dl/launcher";

/// Raw 32-byte Ed25519 public key of the licence service (lowercase hex).
pub const LICENSE_PUBLIC_KEY_HEX: &str =
    "82f710ba58ab744d4aead5b77c096154a4091ff0439f2de422f023b939a2b8b1";

/// "dkk1|MachineGuid|C: serial|hostname" prefix. Frozen: changing it
/// invalidates every machine already activated in the panel.
pub const HWID_PREFIX: &str = "dkk1|";

// ------------------------------------------------------------------- timing
pub const OFFLINE_GRACE_SECONDS: i64 = 24 * 60 * 60;
pub const FRESH_SECONDS: i64 = 30 * 60;
pub const LICENSE_TIMEOUT_MS: u64 = 8_000;
pub const UPDATE_TIMEOUT_MS: u64 = 20_000;
pub const MAX_CORE_BYTES: usize = 128 * 1024 * 1024;
pub const MAX_LAUNCHER_BYTES: usize = 64 * 1024 * 1024;
pub const JAVA_HOST_TIMEOUT_MS: u32 = 30_000;

/// RuntimeHostInjector.dll, base64 (generated from the C++ project's
/// injectorDllData.h; the agent itself is unchanged).
pub const INJECTOR_DLL_B64: &str = include_str!("../assets/injector.b64");
