#include "resourceLoader.h"

#include "installPath.h"
#include "logger.h"
#include "payloadDiagnostics.h"
#include "resource.h"
#include "util.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace dk {

namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime  = 1099511628211ull;

uint64_t fnv1a64(const void* data, size_t len)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = kFnvOffset;
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t fnv1a64File(const std::wstring& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return 0;

    std::vector<uint8_t> buf(65536);
    uint64_t hash = kFnvOffset;
    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()), buf.size());
        const std::streamsize got = in.gcount();
        if (got <= 0)
            break;
        for (std::streamsize i = 0; i < got; ++i) {
            hash ^= buf[static_cast<size_t>(i)];
            hash *= kFnvPrime;
        }
    }
    return hash;
}

uint64_t fileSizeBytes(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        return 0;
    ULARGE_INTEGER sz{};
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    return sz.QuadPart;
}

struct EmbeddedPayload {
    const void* data = nullptr;
    DWORD size = 0;
    uint64_t hash = 0;
};

bool loadEmbedded(EmbeddedPayload& out)
{
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_PAYLOAD_DLL), RT_RCDATA);
    if (!resource)
        return false;

    HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded)
        return false;

    const void* data = LockResource(loaded);
    const DWORD size = SizeofResource(module, resource);
    if (!data || size == 0)
        return false;

    out.data = data;
    out.size = size;
    out.hash = fnv1a64(data, size);
    return true;
}

bool writePayloadFile(const std::wstring& path, const void* data, DWORD size)
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    // Write to .tmp then atomic rename — avoids half-written DLL + looks like normal updater.
    const std::wstring tmp = path + L".tmp";

    HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);

    if (!ok || written != size) {
        DeleteFileW(tmp.c_str());
        return false;
    }

    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }

    return true;
}

std::wstring trySiblingPayload()
{
    auto& log = LauncherLog::I();
    const std::wstring sibling = siblingPayloadPath();
    if (sibling.empty())
        return L"";

    const PayloadDiagnostics diag = diagnosePayload(sibling);
    if (!diag.exists || !diag.readable || !diag.peValid) {
        if (diag.exists)
            log.warn("RuntimeHostCore.dll next to launcher is unreadable or not a valid PE — using embedded core.");
        return L"";
    }

    log.ok("External core DLL (" + std::to_string(diag.fileSize) + " bytes): " + diag.pathUtf8);
    log.info("Drop-in mode: replace this DLL beside the launcher to update without rebuilding the EXE.");
    return sibling;
}

std::wstring installPayload(bool force)
{
    auto& log = LauncherLog::I();

    if (!force) {
        const std::wstring sibling = trySiblingPayload();
        if (!sibling.empty())
            return sibling;
    }

    EmbeddedPayload embedded{};
    if (!loadEmbedded(embedded)) {
        log.error("Embedded payload resource missing — place RuntimeHostCore.dll next to the launcher or rebuild.");
        return L"";
    }

    const std::wstring out = payloadExtractPath(embedded.hash);
    if (out.empty()) {
        log.error("Could not resolve temp extract path.");
        return L"";
    }

    log.info("Single EXE mode — extracting embedded core (" + std::to_string(embedded.size) + " bytes)");

    const bool exists = GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES;
    const uint64_t diskSize = exists ? fileSizeBytes(out) : 0;

    if (exists && !force) {
        if (diskSize != 0 && diskSize != embedded.size) {
            log.warn("Payload size mismatch (disk=" + std::to_string(diskSize)
                + " vs embedded=" + std::to_string(embedded.size) + ") — reinstalling...");
        } else {
            const uint64_t onDisk = fnv1a64File(out);
            if (onDisk != 0 && onDisk == embedded.hash) {
                log.ok("Embedded core ready (" + std::to_string(diskSize) + " bytes, cached in TEMP)");
                return out;
            }
            if (onDisk != 0)
                log.info("Payload update detected (hash changed) — reinstalling...");
            else
                log.warn("Existing payload unreadable — reinstalling...");
        }
    } else if (force) {
        log.info("Forced payload reinstall...");
    } else {
        log.info("First-time payload install...");
    }

    if (!writePayloadFile(out, embedded.data, embedded.size)) {
        log.error("Failed to write payload: " + lastErrorString());
        return L"";
    }

    log.ok("Core extracted from EXE (" + std::to_string(embedded.size) + " bytes)");
    return out;
}

} // namespace

std::wstring ensureEmbeddedPayload()
{
    return installPayload(false);
}

std::wstring forceExtractEmbeddedPayload()
{
    return installPayload(true);
}

DWORD embeddedPayloadSize()
{
    EmbeddedPayload embedded{};
    if (!loadEmbedded(embedded))
        return 0;
    return embedded.size;
}

} // namespace dk
