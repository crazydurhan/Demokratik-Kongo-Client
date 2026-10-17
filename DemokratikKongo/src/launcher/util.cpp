#include "util.h"

namespace dk {

std::string lastErrorString(DWORD err)
{
    if (!err)
        return "(no error)";

    LPSTR msg = nullptr;
    const DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg, 0, nullptr);

    std::string out;
    char prefix[32];
    sprintf_s(prefix, "(0x%08lX) ", err);
    out = prefix;

    if (len && msg) {
        out += msg;
        while (!out.empty() && (out.back() == '\r' || out.back() == '\n'))
            out.pop_back();
        LocalFree(msg);
    } else {
        out += "unknown";
    }
    return out;
}

std::string wideToUtf8(const std::wstring& w)
{
    if (w.empty())
        return {};

    const int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (sz <= 0)
        return {};

    std::string out((size_t)sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), sz, nullptr, nullptr);
    return out;
}

std::vector<uint8_t> decodeBase64(const std::string& in)
{
    static int8_t table[256];
    static bool tableReady = false;
    if (!tableReady) {
        for (int i = 0; i < 256; ++i)
            table[i] = -1;
        const char* alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i)
            table[static_cast<uint8_t>(alphabet[i])] = static_cast<int8_t>(i);
        tableReady = true;
    }

    std::vector<uint8_t> out;
    out.reserve(in.size() / 4 * 3);

    uint32_t buf = 0;
    int bits = 0;
    for (char ch : in) {
        const int8_t v = table[static_cast<uint8_t>(ch)];
        if (v < 0)
            return {}; // malformed input (padding/whitespace) — our generator never emits those
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buf >> bits) & 0xFF));
        }
    }

    return out;
}

} // namespace dk
