//! Minecraft/JVM process discovery and edition filtering.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use windows_sys::Win32::Foundation::{CloseHandle, BOOL, HWND, INVALID_HANDLE_VALUE, LPARAM, TRUE};
use windows_sys::Win32::System::Diagnostics::ToolHelp::{
    CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W, TH32CS_SNAPPROCESS,
};
use windows_sys::Win32::System::Threading::{
    OpenProcess, QueryFullProcessImageNameW, PROCESS_QUERY_LIMITED_INFORMATION,
};
use windows_sys::Win32::UI::WindowsAndMessaging::{
    EnumWindows, GetWindowTextW, GetWindowThreadProcessId, IsWindowVisible,
};

use crate::util::{from_wide, lower_ascii};

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Edition {
    Legacy,
    Modern,
}

impl Edition {
    pub fn tag(self) -> &'static str {
        if self == Edition::Legacy {
            "legacy"
        } else {
            "modern"
        }
    }

    pub fn display(self) -> &'static str {
        if self == Edition::Legacy {
            "1.8.9"
        } else {
            "1.21.x"
        }
    }
}

#[derive(Clone)]
pub struct McProcess {
    pub pid: u32,
    pub exe_name: String,
    #[allow(dead_code)]
    pub exe_path: String,
    pub title: String,
    pub launcher: &'static str,
}

fn is_jvm_host(exe_name: &str) -> bool {
    let lower = lower_ascii(exe_name);
    lower == "java.exe" || lower == "javaw.exe"
}

pub fn is_game_like(text: &str) -> bool {
    let lower = lower_ascii(text);
    [
        "minecraft",
        "lunar",
        "badlion",
        "muzcraft",
        "feather",
        "laby",
        "1.8.",
        "1.21",
        "glfw",
        "lwjgl",
        "javaw",
    ]
    .iter()
    .any(|needle| lower.contains(needle))
}

unsafe extern "system" fn enum_window(hwnd: HWND, lparam: LPARAM) -> BOOL {
    let titles = &mut *(lparam as *mut HashMap<u32, String>);

    let mut pid: u32 = 0;
    GetWindowThreadProcessId(hwnd, &mut pid);
    if pid == 0 {
        return TRUE;
    }

    let mut buffer = [0u16; 512];
    let len = GetWindowTextW(hwnd, buffer.as_mut_ptr(), buffer.len() as i32 - 1);
    if len <= 0 {
        return TRUE;
    }
    let title = from_wide(&buffer[..len as usize]);
    if title.is_empty() {
        return TRUE;
    }

    let visible = IsWindowVisible(hwnd) != 0;
    match titles.get_mut(&pid) {
        Some(existing) => {
            if visible && title.chars().count() > existing.chars().count() {
                *existing = title;
            }
        }
        None => {
            if visible || is_game_like(&title) {
                titles.insert(pid, title);
            }
        }
    }
    TRUE
}

fn window_titles() -> HashMap<u32, String> {
    let mut titles: HashMap<u32, String> = HashMap::new();
    unsafe {
        EnumWindows(
            Some(enum_window),
            &mut titles as *mut HashMap<u32, String> as LPARAM,
        );
    }
    titles
}

fn process_image_path(pid: u32) -> String {
    unsafe {
        let handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid);
        if handle.is_null() || handle == INVALID_HANDLE_VALUE {
            return String::new();
        }
        let mut buffer = [0u16; 1024];
        let mut size = buffer.len() as u32;
        let ok = QueryFullProcessImageNameW(handle, 0, buffer.as_mut_ptr(), &mut size);
        CloseHandle(handle);
        if ok == 0 {
            String::new()
        } else {
            from_wide(&buffer[..size as usize])
        }
    }
}

fn classify(title: &str, exe_path: &str) -> &'static str {
    let haystack = lower_ascii(&format!("{title} {exe_path}"));
    if haystack.contains("badlion") || haystack.contains("blclient") {
        "Badlion"
    } else if haystack.contains("lunar") || haystack.contains("moonsworth") {
        "Lunar"
    } else if haystack.contains("muzcraft")
        || haystack.contains("feather")
        || haystack.contains("laby")
    {
        "Custom"
    } else if haystack.contains("minecraft")
        || haystack.contains("1.8.")
        || haystack.contains("1.21")
    {
        "Vanilla"
    } else {
        "Unknown"
    }
}

/// All JVM/game processes with a window title, unfiltered by edition.
pub fn scan() -> Vec<McProcess> {
    let titles = window_titles();
    let mut result: Vec<McProcess> = Vec::new();
    let mut seen: Vec<u32> = Vec::new();

    unsafe {
        let snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if snapshot != INVALID_HANDLE_VALUE {
            let mut entry: PROCESSENTRY32W = std::mem::zeroed();
            entry.dwSize = std::mem::size_of::<PROCESSENTRY32W>() as u32;
            if Process32FirstW(snapshot, &mut entry) != 0 {
                loop {
                    let pid = entry.th32ProcessID;
                    let exe_name = from_wide(&entry.szExeFile);
                    let title = titles.get(&pid).cloned().unwrap_or_default();

                    let candidate =
                        is_jvm_host(&exe_name) || is_game_like(&exe_name) || is_game_like(&title);
                    if candidate && !seen.contains(&pid) {
                        let exe_path = process_image_path(pid);
                        result.push(McProcess {
                            pid,
                            launcher: classify(&title, &exe_path),
                            exe_name,
                            exe_path,
                            title,
                        });
                        seen.push(pid);
                    }

                    if Process32NextW(snapshot, &mut entry) == 0 {
                        break;
                    }
                }
            }
            CloseHandle(snapshot);
        }
    }

    // Window-first fallback: custom clients may rename the JVM binary so the
    // snapshot executable filter misses them.
    for (pid, title) in &titles {
        if seen.contains(pid) || !is_game_like(title) {
            continue;
        }
        let exe_path = process_image_path(*pid);
        result.push(McProcess {
            pid: *pid,
            launcher: classify(title, &exe_path),
            exe_name: String::new(),
            exe_path,
            title: title.clone(),
        });
        seen.push(*pid);
    }

    result
}

/// 1.8.9 window titles look like "Minecraft 1.8.9" or "Lunar Client 1.8.9";
/// "1.8." deliberately does not match 1.18.x ("1.18.").
pub fn matches_edition(process: &McProcess, edition: Edition) -> bool {
    let legacy = process.title.contains("1.8.");
    match edition {
        Edition::Legacy => legacy,
        Edition::Modern => !legacy,
    }
}

fn push_unique(out: &mut Vec<PathBuf>, candidate: PathBuf) {
    if !candidate.is_file() {
        return;
    }
    let lower = lower_ascii(&candidate.to_string_lossy());
    if out
        .iter()
        .any(|existing| lower_ascii(&existing.to_string_lossy()) == lower)
    {
        return;
    }
    out.push(candidate);
}

fn push_java_in_dir(out: &mut Vec<PathBuf>, dir: &Path) {
    for name in ["javaw.exe", "java.exe"] {
        push_unique(out, dir.join(name));
    }
}

/// Java executables usable as the agent host, best first: the target JVM's own
/// image, then JAVA_HOME / PATH / common JDK install roots.
pub fn collect_java_candidates(pid: u32) -> Vec<PathBuf> {
    let mut out: Vec<PathBuf> = Vec::new();

    let target_image = process_image_path(pid);
    if !target_image.is_empty() {
        let path = PathBuf::from(&target_image);
        let name = lower_ascii(
            path.file_name()
                .and_then(|n| n.to_str())
                .unwrap_or_default(),
        );
        if name == "java.exe" || name == "javaw.exe" {
            push_unique(&mut out, path.clone());
        }
        if let Some(dir) = path.parent() {
            push_java_in_dir(&mut out, dir);
        }
    }

    if let Some(java_home) = std::env::var_os("JAVA_HOME") {
        push_java_in_dir(&mut out, &PathBuf::from(java_home).join("bin"));
    }

    if let Some(path_var) = std::env::var_os("PATH") {
        for dir in std::env::split_paths(&path_var) {
            push_java_in_dir(&mut out, &dir);
        }
    }

    let roots = [
        "C:\\Program Files\\Eclipse Adoptium",
        "C:\\Program Files\\Java",
        "C:\\Program Files\\Zulu",
        "C:\\Program Files\\Amazon Corretto",
        "C:\\Program Files\\BellSoft",
        "C:\\Program Files\\Semeru",
        "C:\\Program Files\\OpenJDK",
        "C:\\Program Files\\Microsoft",
    ];
    for root in roots {
        let Ok(entries) = std::fs::read_dir(root) else {
            continue;
        };
        for entry in entries.flatten() {
            let home = entry.path();
            if !home.is_dir() {
                continue;
            }
            push_java_in_dir(&mut out, &home.join("bin"));
            if let Ok(nested) = std::fs::read_dir(&home) {
                for sub in nested.flatten() {
                    let candidate = sub.path();
                    if candidate.is_dir() {
                        push_java_in_dir(&mut out, &candidate.join("bin"));
                    }
                }
            }
        }
    }

    out
}
