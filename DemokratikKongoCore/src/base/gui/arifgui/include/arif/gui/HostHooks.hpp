// DK-PORT: host hooks for the vendored arifclient ClickGUI.
//
// The Options overlay (gear icon) renders the GUI theme row itself; the host
// application appends the real client options through this hook so the
// vendored tree stays free of game headers.
#pragma once

namespace arif::gui::host {

// Called inside the Options overlay after the theme row. The host composes
// dock-styled rows via arif::gui::panels::hostToggleRow()/... .
void optionsPanelRows();

} // namespace arif::gui::host
