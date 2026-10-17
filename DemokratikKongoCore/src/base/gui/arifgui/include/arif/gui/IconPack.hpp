// ArifClient ClickGUI - PNG ikon paketi (resource/icons, "icon_<ad>.png").
// Ikonlar WIC ile cozulur, beyaz siluete cevrilir (tint ile renklendirilebilir)
// ve GL dokusuna yuklenir. Paket bulunamazsa has()/draw() false doner; cagiran
// taraf vektorel yedek ikonlara duser.
#pragma once

#include <imgui.h>

namespace arif::gui::iconpack {

// GL baglami hazir olduktan sonra bir kez cagrilir. hint: klasor yolu veya nullptr
// (nullptr ise exe'ye ve calisma dizinine gore bilinen yollar denenir).
void initialize(const char* hint = nullptr);
// DK-PORT: forget the uploaded textures WITHOUT touching GL. Used when the
// overlay GL context was already destroyed: the names died with it, and
// glDeleteTextures in the game's context could hit unrelated textures that
// happen to share the same names.
void forget();

void shutdown();

bool has(const char* name);
bool draw(ImDrawList* drawList, const char* name, ImVec2 min, ImVec2 max,
          ImU32 tint = IM_COL32(255, 255, 255, 255));
bool drawCentered(ImDrawList* drawList, const char* name, ImVec2 center, float size,
                  ImU32 tint = IM_COL32(255, 255, 255, 255));
bool drawRotated(ImDrawList* drawList, const char* name, ImVec2 center, float size,
                 float angleRadians, ImU32 tint = IM_COL32(255, 255, 255, 255));

// Modul id -> paket adi ("search" -> "search_grid"; digerleri kimlik eslemesi).
const char* moduleIconName(const char* moduleId);

} // namespace arif::gui::iconpack
