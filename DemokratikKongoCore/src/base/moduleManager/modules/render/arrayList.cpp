#include "arrayList.h"

#include "../../moduleManager.h"
#include "../../../menu/menu.h"
#include "../../../../../ext/imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr float BASE_FONT_SIZE = 14.0f;
    constexpr float PAD_X          = 8.0f;
    constexpr float PAD_Y          = 5.0f;
    constexpr float BORDER_W       = 4.0f;
    constexpr float ROW_GAP        = 2.0f;
    constexpr float CORNER_R       = 4.0f;
    constexpr float SLIDE_PX       = 50.0f;
    constexpr float ANIM_DUR       = 0.20f;
    constexpr ImU32 TAG_COLOR      = IM_COL32(170, 170, 170, 255);
    constexpr ImU32 BASE_TEXT_COLOR = IM_COL32(235, 235, 235, 255);
    constexpr ImU32 BASE_ACCENT = IM_COL32(79, 219, 255, 255);
    constexpr ImU32 BASE_PANEL_BG = IM_COL32(26, 30, 36, 255);
    constexpr ImU32 BASE_DANGER = IM_COL32(220, 80, 80, 255);
    constexpr ImU32 BASE_UTILITY = IM_COL32(255, 206, 94, 255);

    inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

    inline ImU32 withAlpha(ImU32 col, float a)
    {
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
        c.w *= a;
        return ImGui::ColorConvertFloat4ToU32(c);
    }

    inline ImU32 hsv(float h, float s, float v, float a)
    {
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(h - std::floor(h), s, v, r, g, b);
        return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(a * 255));
    }

    inline ImU32 mixSurfaceBg(float alpha)
    {
        return withAlpha(BASE_PANEL_BG, 0.68f * alpha);
    }

    void drawOutlinedText(ImDrawList* dl, ImFont* font, float size,
        ImVec2 pos, ImU32 fill, ImU32 outline, float outlineA, const char* text)
    {
        const ImU32 out = withAlpha(outline, outlineA);
        const float o = 1.0f;
        dl->AddText(font, size, ImVec2(pos.x - o, pos.y), out, text);
        dl->AddText(font, size, ImVec2(pos.x + o, pos.y), out, text);
        dl->AddText(font, size, ImVec2(pos.x, pos.y - o), out, text);
        dl->AddText(font, size, ImVec2(pos.x, pos.y + o), out, text);
        dl->AddText(font, size, pos, fill, text);
    }

    void drawRowBackground(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 bg, bool roundLeft)
    {
        if (roundLeft)
            dl->AddRectFilled(a, b, bg, CORNER_R, ImDrawFlags_RoundCornersLeft);
        else
            dl->AddRectFilled(a, b, bg, CORNER_R, ImDrawFlags_RoundCornersRight);
    }

    void drawSideStripe(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, bool stripeOnRight)
    {
        if (stripeOnRight)
            dl->AddRectFilled(ImVec2(b.x - BORDER_W, a.y), b, col, CORNER_R, ImDrawFlags_RoundCornersTopRight | ImDrawFlags_RoundCornersBottomRight);
        else
            dl->AddRectFilled(a, ImVec2(a.x + BORDER_W, b.y), col, CORNER_R, ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomLeft);
    }
}

ArrayList::ArrayList()
    : Module("ArrayList", "LiquidBounce-style active module list.", Category::Render)
{
    setEnabled(true);

    m_offsetX = &add<NumberSetting>("Offset X", 0.0f, 0.0f, 200.0f, 1.0f);
    m_offsetX->suffix = "px";
    m_offsetY = &add<NumberSetting>("Offset Y", 0.0f, 0.0f, 200.0f, 1.0f);
    m_offsetY->suffix = "px";

    m_showTags = &add<BoolSetting>("Show Tags", true);
    m_itemAlign = &add<EnumSetting>("Item Alignment",
        std::vector<const char*>{ "Left", "Right" }, 1);
    m_order = &add<EnumSetting>("Order",
        std::vector<const char*>{ "Ascending", "Descending" }, 1);
    m_colorMode = &add<EnumSetting>("Color Mode",
        std::vector<const char*>{ "Theme", "Theme Gradient", "Rainbow", "Category" }, 0);
    m_animations = &add<BoolSetting>("Animations", true);
    m_scale = &add<NumberSetting>("Scale", 1.0f, 0.5f, 2.0f, 0.05f);
}

void ArrayList::onRender2D()
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;
    if (screen.x <= 1.f || screen.y <= 1.f) return;

    ImFont* font = (Menu::Font && Menu::Font->IsLoaded()) ? Menu::Font : ImGui::GetFont();
    if (!font) return;

    float dt = io.DeltaTime;
    if (dt > 0.1f) dt = 0.1f;

    const bool animOn = m_animations->value;
    const bool alignRight = (m_itemAlign->index == 1);
    const bool sortAsc = (m_order->index == 0);
    const float scale = m_scale->value;
    const float fontSize = BASE_FONT_SIZE * scale;
    const float padX = PAD_X * scale;
    const float padY = PAD_Y * scale;
    const float borderW = BORDER_W * scale;
    const float rowGap = ROW_GAP * scale;

    m_rainbowPhase += dt * 0.12f;
    if (m_rainbowPhase > 1.f) m_rainbowPhase -= 1.f;

    struct Row {
        Module* mod;
        std::string name;
        std::string tag;
        float width;
        RowAnim* anim;
    };
    std::vector<Row> rows;

    auto& all = ModuleManager::All();
    for (auto& up : all) {
        Module* mod = up.get();
        if (mod == this || !mod->toggleable()) continue;
        if (mod->hideFromArrayList()) continue;

        RowAnim& a = m_rowAnims[mod];
        a.target = mod->isEnabled() ? 1.0f : 0.0f;
        if (animOn) {
            const float k = 1.0f - std::exp(-dt / (ANIM_DUR * 0.33f));
            a.value += (a.target - a.value) * k;
            a.slide += ((1.0f - a.value) * SLIDE_PX - a.slide) * k;
            if (std::fabs(a.target - a.value) < 0.001f) a.value = a.target;
        } else {
            a.value = a.target;
            a.slide = (1.0f - a.value) * SLIDE_PX;
        }

        if (a.value < 0.01f && a.target <= 0.f) continue;

        std::string name = mod->name();
        std::string tag = m_showTags->value ? mod->arrayListSuffix(SuffixDetail::Basic) : "";
        std::string measure = name;
        if (!tag.empty()) measure += " " + tag;
        const float w = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, measure.c_str()).x;
        rows.push_back({ mod, std::move(name), std::move(tag), w, &a });
    }

    if (rows.empty()) return;

    std::sort(rows.begin(), rows.end(), [sortAsc](const Row& a, const Row& b) {
        if (sortAsc) return a.width < b.width;
        return a.width > b.width;
    });

    const float rowH = fontSize + padY * 2.0f;
    float maxBgW = 0.f;
    for (const Row& r : rows) {
        const float bgW = r.width + padX * 2.0f + borderW;
        if (bgW > maxBgW) maxBgW = bgW;
    }

    float totalH = 0.f;
    for (const Row& r : rows)
        totalH += rowH * r.anim->value + rowGap * r.anim->value;

    const float ox = m_offsetX->value;
    const float oy = m_offsetY->value;
    const float colRight = screen.x - ox;
    const float colLeft = colRight - maxBgW;
    float y = oy;

    int visibleIndex = 0;
    for (const Row& r : rows) {
        const float prog = clamp01(r.anim->value);
        if (prog < 0.02f) continue;

        const float slide = r.anim->slide;
        const float rowDrawH = rowH * prog;
        const float bgW = r.width + padX * 2.0f + borderW;

        const float bgX = alignRight ? (colRight - bgW + slide) : (colLeft + slide);

        const ImVec2 bgA{ bgX, y };
        const ImVec2 bgB{ bgX + bgW, y + rowDrawH };

        float hue = m_rainbowPhase + visibleIndex * 0.055f;
        Color overrideCol = r.mod->arrayListColorOverride();
        ImU32 textCol, stripeCol, outlineCol;

        if (overrideCol.a >= 0.0f)
        {
            ImU32 rawCol = IM_COL32((int)(overrideCol.r * 255), (int)(overrideCol.g * 255), (int)(overrideCol.b * 255), 255);
            textCol = withAlpha(rawCol, prog);
            stripeCol = textCol;
            outlineCol = IM_COL32(0, 0, 0, (int)(180 * prog));
        }
        else
        {
            int mode = m_colorMode->index;
            if (mode == 0) // Theme
            {
                textCol = withAlpha(BASE_TEXT_COLOR, prog);
                stripeCol = withAlpha(BASE_ACCENT, prog);
                outlineCol = IM_COL32(0, 0, 0, (int)(180 * prog));
            }
            else if (mode == 1) // Theme Gradient
            {
                float factor = rows.size() > 1 ? (float)visibleIndex / (float)(rows.size() - 1) : 0.0f;
                ImVec4 accVec = ImGui::ColorConvertU32ToFloat4(BASE_ACCENT);
                accVec.x *= (1.0f - 0.4f * factor);
                accVec.y *= (1.0f - 0.4f * factor);
                accVec.z *= (1.0f - 0.4f * factor);
                ImU32 gradientAcc = ImGui::ColorConvertFloat4ToU32(accVec);

                textCol = withAlpha(BASE_TEXT_COLOR, prog);
                stripeCol = withAlpha(gradientAcc, prog);
                outlineCol = IM_COL32(0, 0, 0, (int)(180 * prog));
            }
            else if (mode == 2) // Rainbow
            {
                textCol = hsv(hue, 0.72f, 1.0f, prog);
                stripeCol = hsv(hue + 0.02f, 0.85f, 1.0f, prog);
                outlineCol = hsv(hue + 0.5f, 0.9f, 0.35f, prog * 0.85f);
            }
            else // Category
            {
                ImU32 catCol = BASE_ACCENT;
                switch (r.mod->category())
                {
                    case Category::Combat:   catCol = BASE_DANGER; break;
                    case Category::Movement: catCol = IM_COL32(50, 150, 255, 255); break;
                    case Category::Render:   catCol = IM_COL32(180, 80, 255, 255); break;
                    case Category::Utility:  catCol = BASE_UTILITY; break;
                    case Category::Misc:     catCol = IM_COL32(255, 140, 60, 255); break;
                    default: break;
                }
                textCol = withAlpha(BASE_TEXT_COLOR, prog);
                stripeCol = withAlpha(catCol, prog);
                outlineCol = IM_COL32(0, 0, 0, (int)(180 * prog));
            }
        }

        dl->PushClipRect(bgA, bgB, true);
        drawRowBackground(dl, bgA, bgB, mixSurfaceBg(prog), !alignRight);
        drawSideStripe(dl, bgA, bgB, stripeCol, alignRight);

        const float textX = alignRight
            ? (bgB.x - borderW - padX - r.width)
            : (bgA.x + borderW + padX);
        const float textY = y + padY;

        drawOutlinedText(dl, font, fontSize, ImVec2(textX, textY), textCol, outlineCol, prog, r.name.c_str());

        if (!r.tag.empty()) {
            const float nameW = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, r.name.c_str()).x;
            drawOutlinedText(dl, font, fontSize,
                ImVec2(textX + nameW + 4.0f * scale, textY),
                withAlpha(TAG_COLOR, prog), outlineCol, prog, r.tag.c_str());
        }

        dl->PopClipRect();

        y += rowDrawH + rowGap * prog;
        ++visibleIndex;
    }
}
