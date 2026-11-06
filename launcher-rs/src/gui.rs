//! Minimal launcher GUI (egui). Plain widgets, fixed window, no animation.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use eframe::egui;

use crate::config;
use crate::inject;
use crate::license::{self, LicenseInfo};
use crate::log::{self, Level};
use crate::processes::{self, Edition, McProcess};
use crate::store;
use crate::update;
use crate::util;

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum StatusKind {
    Neutral,
    Ready,
    Busy,
    Ok,
    Warn,
    Error,
}

pub struct Shared {
    pub status: String,
    pub status_kind: StatusKind,
    pub busy: Option<(u32, u32, String)>,
    pub injecting: bool,
    pub license_busy: bool,
    pub license: Option<LicenseInfo>,
    pub processes: Vec<McProcess>,
}

impl Default for Shared {
    fn default() -> Self {
        Self {
            status: "Ready".into(),
            status_kind: StatusKind::Ready,
            busy: None,
            injecting: false,
            license_busy: false,
            license: None,
            processes: Vec::new(),
        }
    }
}

fn lock(shared: &Arc<Mutex<Shared>>) -> std::sync::MutexGuard<'_, Shared> {
    shared.lock().unwrap_or_else(|poisoned| poisoned.into_inner())
}

fn set_busy(shared: &Arc<Mutex<Shared>>, ctx: &egui::Context, step: u32, total: u32, label: &str) {
    if let Some(busy) = lock(shared).busy.as_mut() {
        busy.0 = step;
        busy.1 = total;
        busy.2 = label.to_string();
    }
    ctx.request_repaint();
}

fn finish(shared: &Arc<Mutex<Shared>>, ctx: &egui::Context, message: &str, kind: StatusKind) {
    let mut state = lock(shared);
    state.busy = None;
    state.injecting = false;
    state.status = message.to_string();
    state.status_kind = kind;
    drop(state);
    ctx.request_repaint();
}

fn spawn_license_check(shared: Arc<Mutex<Shared>>, ctx: egui::Context, key: String) {
    {
        let mut state = lock(&shared);
        state.license_busy = true;
        state.status = "Checking license...".into();
        state.status_kind = StatusKind::Busy;
    }
    ctx.request_repaint();

    std::thread::spawn(move || {
        let info = license::check(&key);
        log::log(
            if info.valid { Level::Ok } else { Level::Warn },
            format!("License check: {}", info.user_message),
        );

        let mut state = lock(&shared);
        state.license = Some(info.clone());
        state.license_busy = false;
        state.status = info.user_message.clone();
        state.status_kind = if info.valid {
            StatusKind::Ok
        } else {
            StatusKind::Error
        };
        drop(state);
        ctx.request_repaint();
    });
}

fn spawn_process_scan(shared: Arc<Mutex<Shared>>, ctx: egui::Context) {
    std::thread::spawn(move || {
        let found = processes::scan();
        lock(&shared).processes = found;
        ctx.request_repaint();
    });
}

fn spawn_inject(
    shared: Arc<Mutex<Shared>>,
    ctx: egui::Context,
    pid: u32,
    key: String,
    edition: Edition,
) {
    let stale = {
        let state = lock(&shared);
        let now = util::now_unix();
        state
            .license
            .as_ref()
            .map(|license| now - license.checked_at_unix > config::FRESH_SECONDS)
            .unwrap_or(true)
    };

    let total = if stale { 4 } else { 3 };
    {
        let mut state = lock(&shared);
        state.injecting = true;
        state.busy = Some((0, total, "Preparing...".into()));
        state.status = "Injecting...".into();
        state.status_kind = StatusKind::Busy;
    }
    ctx.request_repaint();

    std::thread::spawn(move || {
        let base: u32 = if stale { 1 } else { 0 };

        if stale {
            set_busy(&shared, &ctx, 1, total, "Verifying license...");
            let info = license::check(&key);
            {
                let mut state = lock(&shared);
                state.license = Some(info.clone());
            }
            if !info.valid {
                log::error(&format!("Inject aborted: {}", info.user_message));
                finish(&shared, &ctx, &info.user_message, StatusKind::Error);
                return;
            }
        }

        set_busy(&shared, &ctx, base + 1, total, "Downloading core...");
        let core = update::fetch_latest_core(&key, edition);
        let (core_path, core_sha) = if core.ok {
            match core.path.clone() {
                Some(path) => {
                    log::info(&format!(
                        "Core update ({}): {} ({} bytes, {})",
                        edition.display(),
                        core.name,
                        core.size,
                        &core.sha[..12.min(core.sha.len())]
                    ));
                    (path, core.sha.clone())
                }
                None => (std::path::PathBuf::new(), String::new()),
            }
        } else {
            let (cached, sha) = update::newest_cached_core(edition);
            match cached {
                Some(path) => {
                    log::warn(&format!(
                        "Core update unavailable ({}) - using cached {} core {}",
                        core.error,
                        edition.display(),
                        sha
                    ));
                    (path, sha)
                }
                None => {
                    log::error(&format!(
                        "Core unavailable for {} and no cached copy ({})",
                        edition.display(),
                        core.error
                    ));
                    finish(
                        &shared,
                        &ctx,
                        "Core unavailable - check connection",
                        StatusKind::Error,
                    );
                    return;
                }
            }
        };

        if core_path.as_os_str().is_empty() {
            finish(
                &shared,
                &ctx,
                "Core unavailable - check connection",
                StatusKind::Error,
            );
            return;
        }

        set_busy(
            &shared,
            &ctx,
            base + 2,
            total,
            "Injecting via java host...",
        );

        let java_candidates = processes::collect_java_candidates(pid);
        let Some(java_exe) = java_candidates.first() else {
            log::error("No java.exe candidate found for the java-hosted injector.");
            finish(
                &shared,
                &ctx,
                "Injection failed - no java.exe",
                StatusKind::Error,
            );
            return;
        };

        let Some(agent_dll) = inject::ensure_agent_dll() else {
            log::error("Embedded injector DLL unavailable.");
            finish(
                &shared,
                &ctx,
                "Injection failed - injector unavailable",
                StatusKind::Error,
            );
            return;
        };

        match inject::run_java_agent(java_exe, &agent_dll, pid, &core_path) {
            Ok(detail) => {
                log::ok(&format!(
                    "Injection complete via java host: {} | payload {} ({})",
                    detail,
                    core_path.display(),
                    &core_sha[..12.min(core_sha.len())]
                ));
                finish(
                    &shared,
                    &ctx,
                    &format!("Injected into PID {pid}"),
                    StatusKind::Ok,
                );
            }
            Err(reason) => {
                log::error(&format!("Injection failed: {reason}"));
                finish(
                    &shared,
                    &ctx,
                    &format!("Injection failed: {reason}"),
                    StatusKind::Error,
                );
            }
        }
    });
}

fn spawn_self_update(shared: Arc<Mutex<Shared>>, ctx: egui::Context) {
    std::thread::spawn(move || {
        let result = update::check_and_apply_launcher_update();
        if result.message.is_empty() {
            return;
        }
        log::info(&format!("Launcher update: {}", result.message));
        if result.updated {
            if let Ok(exe) = std::env::current_exe() {
                let _ = std::process::Command::new(exe).spawn();
            }
            std::process::exit(0);
        }
        let mut state = lock(&shared);
        if state.status_kind != StatusKind::Error {
            state.status = result.message;
        }
        drop(state);
        ctx.request_repaint();
    });
}

pub struct LauncherApp {
    shared: Arc<Mutex<Shared>>,
    edition: Edition,
    key_input: String,
    selected_pid: Option<u32>,
    stop: Arc<AtomicBool>,
}

impl LauncherApp {
    pub fn new(cc: &eframe::CreationContext<'_>) -> Self {
        let ctx = cc.egui_ctx.clone();

        ctx.set_visuals(egui::Visuals::light());
        let mut style = (*ctx.style()).clone();
        style.animation_time = 0.0;
        style.visuals.widgets.noninteractive.rounding = egui::Rounding::ZERO;
        style.visuals.widgets.inactive.rounding = egui::Rounding::ZERO;
        style.visuals.widgets.hovered.rounding = egui::Rounding::ZERO;
        style.visuals.widgets.active.rounding = egui::Rounding::ZERO;
        style.visuals.widgets.open.rounding = egui::Rounding::ZERO;
        ctx.set_style(style);

        let shared = Arc::new(Mutex::new(Shared::default()));
        let stop = Arc::new(AtomicBool::new(false));

        let mut key_input = String::new();
        if let Some(record) = store::load_record() {
            key_input = record.key.clone();
            spawn_license_check(shared.clone(), ctx.clone(), record.key);
        }

        spawn_process_scan(shared.clone(), ctx.clone());
        spawn_self_update(shared.clone(), ctx.clone());

        {
            let shared = shared.clone();
            let ctx = ctx.clone();
            let stop = stop.clone();
            std::thread::spawn(move || {
                while !stop.load(Ordering::Relaxed) {
                    std::thread::sleep(std::time::Duration::from_millis(2500));
                    if stop.load(Ordering::Relaxed) {
                        break;
                    }
                    let found = processes::scan();
                    lock(&shared).processes = found;
                    ctx.request_repaint();
                }
            });
        }

        Self {
            shared,
            edition: Edition::Legacy,
            key_input,
            selected_pid: None,
            stop,
        }
    }

    fn edition_processes(&self) -> Vec<McProcess> {
        let state = lock(&self.shared);
        state
            .processes
            .iter()
            .filter(|process| processes::matches_edition(process, self.edition))
            .cloned()
            .collect()
    }

    fn draw_license(&mut self, ui: &mut egui::Ui) {
        ui.heading("License");

        let (busy, license) = {
            let state = lock(&self.shared);
            (state.license_busy, state.license.clone())
        };

        if busy {
            ui.label("Checking license...");
        } else if let Some(license) = &license {
            if license.valid {
                let mut text = license.user_message.clone();
                if license.offline {
                    text = format!("{} (offline)", text);
                } else if license.days_left >= 0 {
                    text = format!("{} - {} days left", text, license.days_left);
                } else {
                    text = format!("{} - never expires", text);
                }
                ui.colored_label(egui::Color32::from_rgb(20, 110, 40), text);
                if !license.holder.is_empty() {
                    ui.small(format!("Holder: {}", license.holder));
                }
            } else {
                ui.colored_label(egui::Color32::from_rgb(170, 30, 30), &license.user_message);
            }
        } else {
            ui.label("No license stored");
        }

        let ctx = ui.ctx().clone();
        ui.add(
            egui::TextEdit::singleline(&mut self.key_input)
                .hint_text("DKK-XXXXX-XXXXX-XXXXX-XXXXX")
                .desired_width(f32::INFINITY),
        );
        ui.horizontal(|ui| {
            if ui.button("Activate").clicked() {
                let key = license::normalize_key(&self.key_input);
                self.key_input = key.clone();
                if key.is_empty() {
                    let mut state = lock(&self.shared);
                    state.status = "Enter a license key".into();
                    state.status_kind = StatusKind::Warn;
                } else {
                    spawn_license_check(self.shared.clone(), ctx.clone(), key);
                }
            }
            if ui.button("Remove").clicked() {
                store::clear_record();
                self.key_input.clear();
                let mut state = lock(&self.shared);
                state.license = None;
                state.status = "License removed".into();
                state.status_kind = StatusKind::Neutral;
            }
        });
    }

    fn draw_targets(&mut self, ui: &mut egui::Ui) {
        ui.heading("Target");

        let ctx = ui.ctx().clone();
        ui.horizontal(|ui| {
            let mut changed = false;
            if ui
                .selectable_value(&mut self.edition, Edition::Legacy, "1.8.9")
                .clicked()
            {
                changed = true;
            }
            if ui
                .selectable_value(&mut self.edition, Edition::Modern, "1.21.x")
                .clicked()
            {
                changed = true;
            }
            if ui.button("Refresh").clicked() {
                spawn_process_scan(self.shared.clone(), ctx.clone());
            }
            if changed {
                self.selected_pid = None;
            }
        });

        let found = self.edition_processes();
        if found.is_empty() {
            ui.label(format!(
                "No Minecraft {} process found - start the game first",
                self.edition.display()
            ));
            return;
        }

        if let Some(selected) = self.selected_pid {
            if !found.iter().any(|process| process.pid == selected) {
                self.selected_pid = None;
            }
        }

        egui::ScrollArea::vertical()
            .max_height(96.0)
            .auto_shrink([false, true])
            .show(ui, |ui| {
                for process in &found {
                    let selected = self.selected_pid == Some(process.pid);
                    let label = format!(
                        "{}  [PID {}]  {}",
                        if process.title.is_empty() {
                            &process.exe_name
                        } else {
                            &process.title
                        },
                        process.pid,
                        process.launcher
                    );
                    if ui.selectable_label(selected, label).clicked() {
                        self.selected_pid = Some(process.pid);
                    }
                }
            });
    }

    fn draw_inject(&mut self, ui: &mut egui::Ui) {
        let (injecting, busy, license_valid, status, kind) = {
            let state = lock(&self.shared);
            (
                state.injecting,
                state.busy.clone(),
                state
                    .license
                    .as_ref()
                    .map(|license| license.valid && !license.network_error)
                    .unwrap_or(false),
                state.status.clone(),
                state.status_kind,
            )
        };

        let key = license::normalize_key(&self.key_input);
        let can_inject = license_valid && self.selected_pid.is_some() && !injecting && !key.is_empty();
        let ctx = ui.ctx().clone();

        ui.separator();
        ui.horizontal(|ui| {
            if ui
                .add_enabled(can_inject, egui::Button::new("Inject"))
                .clicked()
            {
                if let Some(pid) = self.selected_pid {
                    spawn_inject(self.shared.clone(), ctx.clone(), pid, key.clone(), self.edition);
                }
            }
            if let Some((step, total, label)) = &busy {
                ui.label(format!("{label}  ({step}/{total})"));
            }
        });

        let color = match kind {
            StatusKind::Ok => egui::Color32::from_rgb(20, 110, 40),
            StatusKind::Error => egui::Color32::from_rgb(170, 30, 30),
            StatusKind::Warn => egui::Color32::from_rgb(150, 100, 0),
            _ => ui.style().visuals.text_color(),
        };
        ui.colored_label(color, status);
    }

    fn draw_log(&mut self, ui: &mut egui::Ui) {
        ui.collapsing("Log", |ui| {
            if let Some(path) = log::file_path() {
                ui.small(format!("File: {}", path.display()));
            }
            egui::ScrollArea::vertical()
                .max_height(140.0)
                .auto_shrink([false, true])
                .stick_to_bottom(true)
                .show(ui, |ui| {
                    for entry in log::entries() {
                        ui.label(
                            egui::RichText::new(format!(
                                "{}  [{}] {}",
                                entry.timestamp,
                                entry.level.label(),
                                entry.message
                            ))
                            .monospace(),
                        );
                    }
                });
        });
    }
}

impl eframe::App for LauncherApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        ctx.request_repaint_after(std::time::Duration::from_millis(250));

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.heading("Drip Launcher");
                ui.small(format!("v{}", config::VERSION));
            });
            ui.add_space(4.0);

            self.draw_license(ui);
            ui.add_space(8.0);
            self.draw_targets(ui);
            ui.add_space(8.0);
            self.draw_inject(ui);
            ui.add_space(6.0);
            self.draw_log(ui);
        });
    }
}

impl Drop for LauncherApp {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
    }
}

/// Runs the GUI to completion; returns the process exit code.
pub fn run() -> i32 {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([460.0, 560.0])
            .with_resizable(false)
            .with_title(config::WINDOW_TITLE),
        ..Default::default()
    };

    let result = eframe::run_native(
        "RuntimeHost",
        options,
        Box::new(|cc| Ok(Box::new(LauncherApp::new(cc)))),
    );
    match result {
        Ok(()) => 0,
        Err(error) => {
            log::error(&format!("GUI failed: {error}"));
            1
        }
    }
}
