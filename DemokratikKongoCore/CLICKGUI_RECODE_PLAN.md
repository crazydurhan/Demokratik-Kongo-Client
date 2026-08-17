# ClickGUI Mode Recode Plan

## Status
- **Phase 1** (Fix compile/link): ✅ Complete
- **Phase 2** (Stabilize & sync): ✅ Complete (Phase 0/1 infra done)
- **Phase 2.1+2.2** (Panel + Horizontal recode): ❌ Removed — superseded by slim-down
- **Phase 2.3** (Sadeleştirme + Stabilizasyon): ✅ Complete

## Current Architecture (3 modes only)
- **Classic** (index 0) — sidebar + grid, 5 ThemeStyle sub-themes. Stable.
- **Terminal** (index 1) — komut satırı arayüzü, 3 sub-theme. Right-click context + settings scroll + row hover lift.
- **Floating** (index 2) — serbest sürüklenebilir pencereler, 3 sub-theme. Sub-theme sync + drag fix + settings panel + hover glow.

## Removed Modes (superseded)
- Dropdown, Panel, Horizontal, Radial, Sidebar, Compact, Hologram — deleted completely.

## Terminal Mode — Phase 2.3 Fixes
- Right-click on row → context menu (Toggle / Open Settings / Reset / Add Favorites)
- Settings panel scroll via mouse wheel
- Row hover lift animation (OutQuad 0.10s, ~2px)
- ESC priority: context → settings → clear → close
- Settings panel close on outside click

## Floating Mode — Phase 2.3 Fixes
- **Critical bug fix**: `syncSubTheme()` now called in `init()` and `render()` — `m_activeSubTheme` was never assigned (always 0)
- **Critical bug fix**: Drag logic moved BEFORE `if (win.minimized) return;` — title bar is now draggable even when minimized
- **Bug fix**: Removed local `kThemes[]` array; now uses `GetThemeSpec(GuiMode::Floating, m_activeSubTheme)` from ModeTheme.h (single source of truth)
- Settings panel support: right-click on row opens slide-up settings panel (OutCubic 0.20s)
- Hover glow: window border lerps toward accent on hover (InOutQuad 0.16s)
- Sub-theme differentiation:
  - #0 Acrylic: default + sheen
  - #1 Floating Glass: outer accent glow ring on hover
  - #2 Solid Snap: grid snap on drag release (20px grid)

## Remaining Work (future)
- Fine-tune specific mode behaviors
- Possible new modes in future
