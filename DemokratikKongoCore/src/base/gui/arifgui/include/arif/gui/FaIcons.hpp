// ArifClient ClickGUI - Font Awesome 6 Free (Solid) ikon kod noktalari.
// Kaynak: @fortawesome/fontawesome-free 6.7.2 (CC BY 4.0) metadata.
// IconPack bu tabloyu kullanir: once FA glifi, sonra PNG paketi, en son vektor.
#pragma once

#include <cstddef>
#include <cstdint>

namespace arif::gui::fa {

struct Entry {
    const char* name;
    std::uint16_t codepoint;
};

// Isim -> kod noktasi tablosu (kayit sayisi ile birlikte).
const Entry* entries(std::size_t& count);

// Ad -> Font Awesome kod noktasi (bulunamazsa 0).
std::uint16_t codepoint(const char* name);

} // namespace arif::gui::fa
