//! Launcher log: in-memory ring (for the GUI) + append-only file.

use std::io::Write;
use std::path::PathBuf;
use std::sync::{Mutex, OnceLock};

use crate::util;

#[derive(Clone, Copy, PartialEq, Eq)]
#[allow(dead_code)]
pub enum Level {
    Trace,
    Debug,
    Info,
    Ok,
    Warn,
    Error,
}

impl Level {
    pub fn label(self) -> &'static str {
        match self {
            Level::Trace => "TRACE",
            Level::Debug => "DEBUG",
            Level::Info => "INFO",
            Level::Ok => "OK",
            Level::Warn => "WARN",
            Level::Error => "ERROR",
        }
    }
}

#[derive(Clone)]
pub struct Entry {
    pub level: Level,
    pub timestamp: String,
    pub message: String,
}

struct State {
    entries: Vec<Entry>,
    file: Option<PathBuf>,
    enabled: bool,
}

const MAX_ENTRIES: usize = 800;

fn state() -> &'static Mutex<State> {
    static STATE: OnceLock<Mutex<State>> = OnceLock::new();
    STATE.get_or_init(|| {
        Mutex::new(State {
            entries: Vec::new(),
            file: util::log_path(),
            enabled: true,
        })
    })
}

fn timestamp_now() -> String {
    chrono::Local::now().format("%H:%M:%S%.3f").to_string()
}

pub fn init() {
    if let Some(dir) = util::appdata_dir() {
        let _ = util::ensure_dir(&dir);
    }
    log(Level::Info, "Launcher started");
}

pub fn log(level: Level, message: impl Into<String>) {
    let entry = Entry {
        level,
        timestamp: timestamp_now(),
        message: message.into(),
    };

    let mut guard = match state().lock() {
        Ok(guard) => guard,
        Err(poisoned) => poisoned.into_inner(),
    };

    let line = format!(
        "[{}] [{}] {}",
        entry.timestamp,
        entry.level.label(),
        entry.message
    );
    if guard.enabled {
        if let Some(path) = guard.file.clone() {
            if let Ok(mut file) = std::fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open(path)
            {
                let _ = writeln!(file, "{line}");
            }
        }
    }

    guard.entries.push(entry);
    if guard.entries.len() > MAX_ENTRIES {
        let overflow = guard.entries.len() - MAX_ENTRIES;
        guard.entries.drain(0..overflow);
    }
}

/// Snapshot for the GUI (newest last).
pub fn entries() -> Vec<Entry> {
    match state().lock() {
        Ok(guard) => guard.entries.clone(),
        Err(poisoned) => poisoned.into_inner().entries.clone(),
    }
}

pub fn info(message: &str) {
    log(Level::Info, message);
}
pub fn ok(message: &str) {
    log(Level::Ok, message);
}
pub fn warn(message: &str) {
    log(Level::Warn, message);
}
pub fn error(message: &str) {
    log(Level::Error, message);
}

pub fn file_path() -> Option<PathBuf> {
    match state().lock() {
        Ok(guard) => guard.file.clone(),
        Err(poisoned) => poisoned.into_inner().file.clone(),
    }
}

#[macro_export]
macro_rules! logf {
    ($level:expr, $($arg:tt)*) => {
        $crate::log::log($level, format!($($arg)*))
    };
}
