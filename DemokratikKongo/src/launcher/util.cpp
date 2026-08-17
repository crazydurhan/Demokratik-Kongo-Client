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

} // namespace dk
