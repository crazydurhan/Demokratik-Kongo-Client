#pragma once

#include <cstdint>
#include <string>

namespace dk {

// Extract embedded core to a temp cache keyed by payload hash.
std::wstring payloadExtractPath(uint64_t embeddedContentHash);

// DemokratikKongoCore.dll next to this launcher EXE (optional external payload).
std::wstring siblingPayloadPath();

} // namespace dk
