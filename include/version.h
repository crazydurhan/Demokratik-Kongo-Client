#pragma once

// Single source of truth for the product version.
//
// Included by both C++ translation units and injector/resources/version.rc.
// The Windows resource compiler only understands object-like macros, so the
// C++ constants below are guarded with RC_INVOKED.

#define DK_VERSION_MAJOR 1
#define DK_VERSION_MINOR 1
#define DK_VERSION_PATCH 0
#define DK_VERSION_BUILD 0

// RC wants a comma-separated tuple for FILEVERSION/PRODUCTVERSION.
#define DK_VERSION_COMMA DK_VERSION_MAJOR, DK_VERSION_MINOR, DK_VERSION_PATCH, DK_VERSION_BUILD

// Two-step stringification so the macro arguments expand before quoting.
#define DK_STRINGIFY_IMPL(x) #x
#define DK_STRINGIFY(x) DK_STRINGIFY_IMPL(x)

#define DK_VERSION_STR                                                                                                 \
    DK_STRINGIFY(DK_VERSION_MAJOR)                                                                                     \
    "." DK_STRINGIFY(DK_VERSION_MINOR) "." DK_STRINGIFY(DK_VERSION_PATCH) "." DK_STRINGIFY(DK_VERSION_BUILD)

// Short "1.1.0" form for UI surfaces where the build field is noise.
#define DK_VERSION_SHORT_STR                                                                                           \
    DK_STRINGIFY(DK_VERSION_MAJOR)                                                                                     \
    "." DK_STRINGIFY(DK_VERSION_MINOR) "." DK_STRINGIFY(DK_VERSION_PATCH)

#ifndef RC_INVOKED

namespace dk::version {

inline constexpr int kMajor = DK_VERSION_MAJOR;
inline constexpr int kMinor = DK_VERSION_MINOR;
inline constexpr int kPatch = DK_VERSION_PATCH;
inline constexpr int kBuild = DK_VERSION_BUILD;

inline constexpr char kString[]      = DK_VERSION_STR;
inline constexpr char kShortString[] = DK_VERSION_SHORT_STR;

} // namespace dk::version

#endif // RC_INVOKED
