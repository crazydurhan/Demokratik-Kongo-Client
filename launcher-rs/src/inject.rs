//! java.exe-hosted injection: the remote load is performed by a JVM tool
//! process running the embedded JVMTI agent, never by the launcher itself.

use std::path::{Path, PathBuf};

use windows_sys::Win32::Foundation::{CloseHandle, WAIT_OBJECT_0};
use windows_sys::Win32::System::Threading::{
    CreateProcessW, GetExitCodeProcess, WaitForSingleObject, CREATE_NO_WINDOW,
    PROCESS_INFORMATION, STARTUPINFOW,
};

use crate::config;
use crate::util;

/// Extract the embedded injector DLL into a content-addressed temp folder and
/// return its path.
pub fn ensure_agent_dll() -> Option<PathBuf> {
    let bytes = util::b64_decode_standard(config::INJECTOR_DLL_B64)?;
    if bytes.is_empty() {
        return None;
    }

    let hash = util::fnv1a64(&bytes);
    let dir = std::env::temp_dir()
        .join(config::APP_FOLDER)
        .join(format!("{hash:016x}"));
    if !util::ensure_dir(&dir) {
        return None;
    }

    let path = dir.join(config::INJECTOR_DLL);
    let current_size = std::fs::metadata(&path).map(|m| m.len()).unwrap_or(0);
    if current_size != bytes.len() as u64 && !util::write_atomic(&path, &bytes) {
        return None;
    }
    Some(path)
}

/// Runs `java -agentpath:<agent>=pid=<pid>;dll=<payload> -version` hidden and
/// waits for the host to finish. Ok(detail) on a zero exit code.
pub fn run_java_agent(
    java_exe: &Path,
    agent_dll: &Path,
    target_pid: u32,
    payload_dll: &Path,
) -> Result<String, String> {
    if target_pid == 0 {
        return Err("invalid arguments".into());
    }

    let command = format!(
        "\"{}\" \"-agentpath:{}=pid={};dll={}\" -version",
        java_exe.display(),
        agent_dll.display(),
        target_pid,
        payload_dll.display()
    );

    let mut wide_command = util::wide(&command);
    let mut startup: STARTUPINFOW = unsafe { std::mem::zeroed() };
    startup.cb = std::mem::size_of::<STARTUPINFOW>() as u32;
    let mut process: PROCESS_INFORMATION = unsafe { std::mem::zeroed() };

    let created = unsafe {
        CreateProcessW(
            std::ptr::null(),
            wide_command.as_mut_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            0,
            CREATE_NO_WINDOW,
            std::ptr::null(),
            std::ptr::null(),
            &startup,
            &mut process,
        )
    };
    if created == 0 {
        return Err(format!(
            "CreateProcess failed: {}",
            std::io::Error::last_os_error()
        ));
    }

    let wait = unsafe { WaitForSingleObject(process.hProcess, config::JAVA_HOST_TIMEOUT_MS) };
    let mut exit_code: u32 = 0;
    unsafe {
        GetExitCodeProcess(process.hProcess, &mut exit_code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }

    if wait != WAIT_OBJECT_0 {
        return Err("java host timed out".into());
    }
    if exit_code != 0 {
        return Err(format!(
            "java agent exit code {exit_code} (see inject-agent.log)"
        ));
    }

    Ok(format!("hosted by {}", java_exe.display()))
}
