#pragma once

#include <string>

namespace dk {

// RuntimeHostCore.dll next to this launcher EXE (optional drop-in fallback
// when the update service cannot be reached and nothing is cached yet).
std::wstring siblingPayloadPath();

} // namespace dk
