#pragma once

namespace Gui
{
    // Category columns (Vape-style dropdown): a row of detachable panels,
    // one per module category, drawn instead of the classic shell when
    // Preferences::menuLayout == 1.
    void RenderDropdown(float opacity);

    // Clears search, open panels, and dragged column positions.
    void ResetDropdown();
}
