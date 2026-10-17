#include "arif/gui/Icons.hpp"

#include <cmath>

#include "arif/gui/Theme.hpp"

namespace arif::gui::icons {

namespace {

void drawSword(ImDrawList* dl, ImVec2 c, float s, ImU32 color) {
    const ImVec2 hilt(c.x - 0.55f * s, c.y + 0.55f * s);
    const ImVec2 tip(c.x + 0.65f * s, c.y - 0.65f * s);
    dl->AddLine(hilt, tip, color, 1.5f);
    const ImVec2 guardCenter(c.x - 0.286f * s, c.y + 0.286f * s);
    dl->AddLine(ImVec2(guardCenter.x - 0.184f * s, guardCenter.y - 0.184f * s),
                ImVec2(guardCenter.x + 0.184f * s, guardCenter.y + 0.184f * s), color, 1.5f);
    dl->AddCircleFilled(hilt, 0.12f * s, color, 10);
}

void drawEye(ImDrawList* dl, ImVec2 c, float s, ImU32 color) {
    dl->PathClear();
    dl->PathArcTo(c, s * 0.85f, kPi * 1.15f, kPi * 1.85f, 18);
    dl->PathStroke(color, 0, 1.5f);
    dl->PathClear();
    dl->PathArcTo(c, s * 0.85f, kPi * 0.15f, kPi * 0.85f, 18);
    dl->PathStroke(color, 0, 1.5f);
    dl->AddCircleFilled(c, s * 0.24f, color, 14);
}

void drawPerson(ImDrawList* dl, ImVec2 c, float s, ImU32 color) {
    dl->AddCircleFilled(ImVec2(c.x, c.y - 0.38f * s), s * 0.30f, color, 18);
    dl->PathClear();
    dl->PathArcTo(ImVec2(c.x, c.y + 0.78f * s), s * 0.55f, kPi * 1.05f, kPi * 1.95f, 20);
    dl->PathStroke(color, 0, 1.5f);
}

} // namespace

// ---------------------------------------------------------------------------
// Modul ikonlari
// ---------------------------------------------------------------------------
void drawModuleIcon(ImDrawList* dl, ModuleIcon icon, ImVec2 c, float s, ImU32 color) {
    switch (icon) {
        case ModuleIcon::Sun: {
            dl->AddCircleFilled(c, 0.42f * s, color, 20);
            for (int i = 0; i < 8; ++i) {
                const float a = (float)i * kPi * 0.25f;
                const ImVec2 dir(std::cos(a), std::sin(a));
                dl->AddLine(ImVec2(c.x + dir.x * 0.62f * s, c.y + dir.y * 0.62f * s),
                            ImVec2(c.x + dir.x * 0.92f * s, c.y + dir.y * 0.92f * s), color, 1.4f);
            }
            break;
        }
        case ModuleIcon::Fist: {
            dl->AddRect(ImVec2(c.x - 0.58f * s, c.y - 0.34f * s),
                        ImVec2(c.x + 0.58f * s, c.y + 0.56f * s), color, 3.0f, 0, 1.5f);
            for (int k = -1; k <= 1; ++k) {
                const float x = c.x + (float)k * 0.28f * s;
                dl->AddLine(ImVec2(x, c.y - 0.34f * s), ImVec2(x, c.y - 0.02f * s), color, 1.2f);
            }
            dl->AddRect(ImVec2(c.x - 0.84f * s, c.y - 0.10f * s),
                        ImVec2(c.x - 0.60f * s, c.y + 0.32f * s), color, 2.0f, 0, 1.4f);
            break;
        }
        case ModuleIcon::Potion: {
            dl->AddCircle(c, 0.52f * s, color, 24, 1.5f);
            dl->AddRect(ImVec2(c.x - 0.17f * s, c.y - 0.85f * s),
                        ImVec2(c.x + 0.17f * s, c.y - 0.42f * s), color, 1.5f, 0, 1.4f);
            dl->AddLine(ImVec2(c.x - 0.28f * s, c.y - 0.78f * s),
                        ImVec2(c.x + 0.28f * s, c.y - 0.78f * s), color, 2.0f);
            dl->PathClear();
            dl->PathArcTo(c, 0.44f * s, kPi * 0.15f, kPi * 0.85f, 16);
            dl->PathFillConvex(color);
            break;
        }
        case ModuleIcon::Arrows: {
            dl->AddLine(ImVec2(c.x - 0.75f * s, c.y - 0.22f * s),
                        ImVec2(c.x + 0.55f * s, c.y - 0.22f * s), color, 1.5f);
            dl->AddLine(ImVec2(c.x + 0.55f * s, c.y - 0.22f * s),
                        ImVec2(c.x + 0.25f * s, c.y - 0.50f * s), color, 1.5f);
            dl->AddLine(ImVec2(c.x + 0.55f * s, c.y - 0.22f * s),
                        ImVec2(c.x + 0.25f * s, c.y + 0.06f * s), color, 1.5f);
            dl->AddLine(ImVec2(c.x + 0.75f * s, c.y + 0.28f * s),
                        ImVec2(c.x - 0.55f * s, c.y + 0.28f * s), color, 1.5f);
            dl->AddLine(ImVec2(c.x - 0.55f * s, c.y + 0.28f * s),
                        ImVec2(c.x - 0.25f * s, c.y), color, 1.5f);
            dl->AddLine(ImVec2(c.x - 0.55f * s, c.y + 0.28f * s),
                        ImVec2(c.x - 0.25f * s, c.y + 0.56f * s), color, 1.5f);
            break;
        }
        case ModuleIcon::Teleport: {
            dl->AddQuad(ImVec2(c.x, c.y - 0.68f * s), ImVec2(c.x + 0.52f * s, c.y),
                        ImVec2(c.x, c.y + 0.68f * s), ImVec2(c.x - 0.52f * s, c.y), color, 1.5f);
            dl->AddCircleFilled(c, 0.15f * s, color, 10);
            dl->AddLine(ImVec2(c.x + 0.72f * s, c.y - 0.38f * s),
                        ImVec2(c.x + 0.9f * s, c.y - 0.58f * s), color, 1.2f);
            dl->AddLine(ImVec2(c.x + 0.78f * s, c.y + 0.05f * s),
                        ImVec2(c.x + 0.98f * s, c.y + 0.05f * s), color, 1.2f);
            break;
        }
        case ModuleIcon::Sword:
            drawSword(dl, c, s, color);
            break;
        case ModuleIcon::Eye:
            drawEye(dl, c, s, color);
            break;
        case ModuleIcon::Person:
            drawPerson(dl, c, s, color);
            break;
        case ModuleIcon::Search: {
            dl->AddCircle(ImVec2(c.x - 0.12f * s, c.y - 0.12f * s), 0.5f * s, color, 20, 1.6f);
            dl->AddLine(ImVec2(c.x + 0.24f * s, c.y + 0.24f * s),
                        ImVec2(c.x + 0.68f * s, c.y + 0.68f * s), color, 1.6f);
            break;
        }
        case ModuleIcon::Generic:
        default: {
            dl->AddRect(ImVec2(c.x - 0.5f * s, c.y - 0.5f * s),
                        ImVec2(c.x + 0.5f * s, c.y + 0.5f * s), color, 3.0f, 0, 1.5f);
            dl->AddCircleFilled(c, 0.16f * s, color, 10);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// UI / pencere kontrol ikonlari
// ---------------------------------------------------------------------------
void drawUiIcon(ImDrawList* dl, UiIcon icon, ImVec2 c, float s, ImU32 color, float rotation) {
    switch (icon) {
        case UiIcon::Home: {
            dl->AddLine(ImVec2(c.x - 0.72f * s, c.y - 0.02f * s), ImVec2(c.x, c.y - 0.7f * s), color, 1.6f);
            dl->AddLine(ImVec2(c.x, c.y - 0.7f * s), ImVec2(c.x + 0.72f * s, c.y - 0.02f * s), color, 1.6f);
            dl->AddRect(ImVec2(c.x - 0.45f * s, c.y - 0.05f * s),
                        ImVec2(c.x + 0.45f * s, c.y + 0.62f * s), color, 1.5f, 0, 1.5f);
            break;
        }
        case UiIcon::Profiles: {
            dl->AddRect(ImVec2(c.x - 0.68f * s, c.y - 0.62f * s),
                        ImVec2(c.x + 0.34f * s, c.y + 0.28f * s), color, 2.0f, 0, 1.4f);
            dl->AddRect(ImVec2(c.x - 0.34f * s, c.y - 0.28f * s),
                        ImVec2(c.x + 0.68f * s, c.y + 0.62f * s), color, 2.0f, 0, 1.4f);
            break;
        }
        case UiIcon::Search: {
            dl->AddCircle(ImVec2(c.x - 0.12f * s, c.y - 0.12f * s), 0.5f * s, color, 20, 1.6f);
            dl->AddLine(ImVec2(c.x + 0.24f * s, c.y + 0.24f * s),
                        ImVec2(c.x + 0.68f * s, c.y + 0.68f * s), color, 1.6f);
            break;
        }
        case UiIcon::Scripts: {
            dl->AddRect(ImVec2(c.x - 0.5f * s, c.y - 0.7f * s),
                        ImVec2(c.x + 0.5f * s, c.y + 0.7f * s), color, 2.0f, 0, 1.4f);
            for (int i = -1; i <= 1; ++i) {
                const float y = c.y + (float)i * 0.32f * s;
                dl->AddLine(ImVec2(c.x - 0.26f * s, y), ImVec2(c.x + 0.26f * s, y), color, 1.2f);
            }
            break;
        }
        case UiIcon::Gear: {
            dl->AddCircle(c, 0.36f * s, color, 20, 1.5f);
            for (int i = 0; i < 6; ++i) {
                const float a = rotation + (float)i * kPi / 3.0f;
                const ImVec2 dir(std::cos(a), std::sin(a));
                dl->AddLine(ImVec2(c.x + dir.x * 0.54f * s, c.y + dir.y * 0.54f * s),
                            ImVec2(c.x + dir.x * 0.86f * s, c.y + dir.y * 0.86f * s), color, 2.0f);
            }
            dl->AddCircleFilled(c, 0.12f * s, color, 10);
            break;
        }
        case UiIcon::Refresh: {
            const float endAngle = rotation + kPi * 1.65f;
            dl->PathClear();
            dl->PathArcTo(c, 0.58f * s, rotation, endAngle, 28);
            dl->PathStroke(color, 0, 1.6f);

            const ImVec2 tip(c.x + std::cos(endAngle) * 0.58f * s, c.y + std::sin(endAngle) * 0.58f * s);
            const ImVec2 tangent(-std::sin(endAngle), std::cos(endAngle));
            const ImVec2 radial(std::cos(endAngle), std::sin(endAngle));
            const ImVec2 base(tip.x - tangent.x * 0.26f * s, tip.y - tangent.y * 0.26f * s);
            dl->AddTriangleFilled(tip,
                                  ImVec2(base.x + radial.x * 0.14f * s, base.y + radial.y * 0.14f * s),
                                  ImVec2(base.x - radial.x * 0.14f * s, base.y - radial.y * 0.14f * s),
                                  color);
            break;
        }
        case UiIcon::Minimize:
            dl->AddLine(ImVec2(c.x - 0.5f * s, c.y), ImVec2(c.x + 0.5f * s, c.y), color, 1.6f);
            break;
        case UiIcon::Close:
            dl->AddLine(ImVec2(c.x - 0.45f * s, c.y - 0.45f * s),
                        ImVec2(c.x + 0.45f * s, c.y + 0.45f * s), color, 1.6f);
            dl->AddLine(ImVec2(c.x + 0.45f * s, c.y - 0.45f * s),
                        ImVec2(c.x - 0.45f * s, c.y + 0.45f * s), color, 1.6f);
            break;
        case UiIcon::More:
            for (int i = -1; i <= 1; ++i) {
                dl->AddCircleFilled(ImVec2(c.x, c.y + (float)i * 0.42f * s), 0.14f * s, color, 10);
            }
            break;
        case UiIcon::Back: {
            dl->AddLine(ImVec2(c.x + 0.45f * s, c.y - 0.55f * s),
                        ImVec2(c.x - 0.2f * s, c.y), color, 1.6f);
            dl->AddLine(ImVec2(c.x - 0.2f * s, c.y),
                        ImVec2(c.x + 0.45f * s, c.y + 0.55f * s), color, 1.6f);
            dl->AddLine(ImVec2(c.x - 0.25f * s, c.y),
                        ImVec2(c.x + 0.6f * s, c.y), color, 1.6f);
            break;
        }
        case UiIcon::Star: {
            ImVec2 points[10];
            for (int i = 0; i < 10; ++i) {
                const float radius = (i % 2 == 0) ? 0.72f * s : 0.30f * s;
                const float angle = -kPi * 0.5f + (float)i * kPi * 0.2f;
                points[i] = ImVec2(c.x + std::cos(angle) * radius, c.y + std::sin(angle) * radius);
            }
            dl->AddPolyline(points, 10, color, ImDrawFlags_Closed, 1.5f);
            break;
        }
    }
}

} // namespace arif::gui::icons
