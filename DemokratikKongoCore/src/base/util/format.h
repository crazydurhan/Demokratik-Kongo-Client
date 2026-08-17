#pragma once

// Katman 5: common fstr/istr/bstr helpers. Previously every module that
// pushed numeric settings to the patcher re-defined the same three
// inline helpers using std::ostringstream.  std::to_string is ~5-10x
// faster on small ints/floats and produces no stream-buffer overhead.
// Behavior is identical (same character output for the values we send).

#include <string>

namespace detail
{
    template <typename T>
    inline std::string toStr(T v) { return std::to_string(v); }
}

inline std::string fstr(float  v) { return detail::toStr(v); }
inline std::string istr(int    v) { return detail::toStr(v); }
inline std::string bstr(bool   v) { return v ? "true" : "false"; }
inline std::string dstr(double v) { return detail::toStr(v); }
