// ArifClient ClickGUI - Font Awesome 6 Free (Solid) isim -> kod noktasi tablosu.
// Uretim: @fortawesome/fontawesome-free 6.7.2 metadata'sindan otomatik alindi
// (docs/ICON_SPEC.md - "Font Awesome eslemeleri").
#include "arif/gui/FaIcons.hpp"

#include <cstring>

namespace arif::gui::fa {

const Entry* entries(std::size_t& count) {
    // FA 6'daki karsiliklar: home=house, profiles=id-card, scripts=file-code,
    // refresh=arrows-rotate, search=magnifying-glass, search_grid=table-cells,
    // combat=hand-fist, utility=screwdriver-wrench, velocity=gauge-high,
    // rightclicker=computer-mouse, autoclicker=hand-pointer, reach=ruler,
    // killaura=skull, criticals=bolt, tracers=route, refill=fill-drip.
    static const Entry kEntries[] = {
        {"home", 0xf015},         // house
        {"profiles", 0xf2c2},     // id-card
        {"scripts", 0xf1c9},      // file-code
        {"settings", 0xf013},     // gear
        {"refresh", 0xf021},      // arrows-rotate
        {"back", 0xf060},         // arrow-left
        {"favorites", 0xf005},    // star
        {"minimize", 0xf068},     // minus
        {"close", 0xf00d},        // xmark
        {"search", 0xf002},       // magnifying-glass
        {"search_grid", 0xf00a},  // table-cells
        {"combat", 0xf6de},       // hand-fist
        {"render", 0xf53f},       // palette
        {"utility", 0xf7d9},      // screwdriver-wrench
        {"modules", 0xf1b3},      // cubes
        {"velocity", 0xf625},    // gauge-high
        {"nametags", 0xf02c},     // tags
        {"throwpot", 0xf0c3},     // flask
        {"rightclicker", 0xf8cc}, // computer-mouse
        {"autoclicker", 0xf25a},  // hand-pointer
        {"aimassist", 0xf05b},    // crosshairs
        {"reach", 0xf545},        // ruler
        {"killaura", 0xf54c},     // skull
        {"criticals", 0xf0e7},    // bolt
        {"tracers", 0xf4d7},      // route
        {"fullbright", 0xf185},   // sun
        {"esp", 0xf06e},          // eye
        {"storageesp", 0xf49e},   // box-open
        {"refill", 0xf576},       // fill-drip
        {"blink", 0xf6e2},        // ghost
        {"sprint", 0xf70c},       // person-running
        {"fastplace", 0xf1b2},    // cube
    };
    count = sizeof(kEntries) / sizeof(kEntries[0]);
    return kEntries;
}

std::uint16_t codepoint(const char* name) {
    if (name == nullptr) {
        return 0;
    }
    std::size_t count = 0;
    const Entry* table = entries(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (std::strcmp(table[i].name, name) == 0) {
            return table[i].codepoint;
        }
    }
    return 0;
}

} // namespace arif::gui::fa
