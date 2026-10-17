// ArifClient ClickGUI - PNG ikon paketi yukleyicisi.
//
//   embedded arif_icon_blobs[]  ->  "<ad>" anahtariyla GL dokusu
//
// DK-PORT: the standalone arifclient build scanned resource\icons\icon_*.png
// on disk. The injected core has no resource folder, so the exact same PNG
// bytes are embedded by tools/generate-arif-assets.ps1 (ext/arifgui/
// arif_iconpack.h) and decoded from memory. The WIC decode pipeline (alpha
// bounding box, aspect-fit into 128x128, white + coverage alpha, CPU mip
// chain) is byte-for-byte the original.
//
// Renk beyaz sabittir; boylece ImDrawList tint renkleri vektorel ikonlarla
// ayni sekilde calisir.
#include "arif/gui/IconPack.hpp"

#include "arif/gui/FaIcons.hpp"
#include "arif/gui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <GL/gl.h>

// DK-PORT: embedded icon pack (generated).
#include "arif_iconpack.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F   // GL 1.2; Windows gl.h (1.1) tanimlamaz
#endif

namespace arif::gui::iconpack {
namespace {

constexpr int kIconSize = 128;    // doku boyutu (CPU'da kucultulur)
constexpr int kProbe = 256;       // sinir kutusu tespiti icin on izleme boyutu
constexpr int kAlphaFloor = 6;    // bu esigin alti "bos" sayilir

std::unordered_map<std::string, GLuint>& textures() {
    static std::unordered_map<std::string, GLuint> map;
    return map;
}

bool g_comInitialized = false;

// Tek bir PNG'yi okuyup beyaz siluet dokusuna cevirir.
//
//  1. On izleme (256x256) ile alfa sinir kutusu bulunur.
//  2. Kirpilir; glif, en-boy korunarak 128x128 tuvale sigdirilir.
//  3. Alfa kanali kapsama olarak kopyalanir (renk beyaz sabit).
//
// DK-PORT: decodes from an in-memory blob (SHCreateMemStream) instead of a
// file path; everything else matches the original implementation.
GLuint loadIcon(IWICImagingFactory* factory, const unsigned char* bytes, unsigned int size) {
    if (factory == nullptr || bytes == nullptr || size == 0) {
        return 0;
    }

    IStream* stream = SHCreateMemStream(bytes, size);
    if (stream == nullptr) {
        return 0;
    }

    GLuint texture = 0;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* probe = nullptr;
    IWICFormatConverter* probeConverter = nullptr;
    IWICBitmapClipper* clipper = nullptr;
    IWICBitmapScaler* fit = nullptr;
    IWICFormatConverter* converter = nullptr;

    do {
        if (FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                                    &decoder))) {
            break;
        }
        if (FAILED(decoder->GetFrame(0, &frame))) {
            break;
        }

        UINT sourceWidth = 0;
        UINT sourceHeight = 0;
        if (FAILED(frame->GetSize(&sourceWidth, &sourceHeight)) || sourceWidth == 0 || sourceHeight == 0) {
            break;
        }

        // 1) Alfa sinir kutusu (on izleme uzerinde).
        if (FAILED(factory->CreateBitmapScaler(&probe)) ||
            FAILED(probe->Initialize(frame, kProbe, kProbe, WICBitmapInterpolationModeFant)) ||
            FAILED(factory->CreateFormatConverter(&probeConverter)) ||
            FAILED(probeConverter->Initialize(probe, GUID_WICPixelFormat32bppRGBA,
                                              WICBitmapDitherTypeNone, nullptr, 0.0,
                                              WICBitmapPaletteTypeCustom))) {
            break;
        }

        std::vector<unsigned char> probePixels((size_t)kProbe * kProbe * 4);
        if (FAILED(probeConverter->CopyPixels(nullptr, kProbe * 4, (UINT)probePixels.size(),
                                              probePixels.data()))) {
            break;
        }

        int minX = kProbe;
        int minY = kProbe;
        int maxX = -1;
        int maxY = -1;
        for (int y = 0; y < kProbe; ++y) {
            for (int x = 0; x < kProbe; ++x) {
                if (probePixels[((size_t)y * kProbe + x) * 4 + 3] > kAlphaFloor) {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        }
        if (maxX < 0) {   // tamamen seffaf: ikon yok
            break;
        }

        const int pad = 2;   // probe uzayinda kenar payi
        const float scaleX = (float)sourceWidth / (float)kProbe;
        const float scaleY = (float)sourceHeight / (float)kProbe;
        WICRect clipRect = {};
        clipRect.X = std::max(0, (int)((minX - pad) * scaleX));
        clipRect.Y = std::max(0, (int)((minY - pad) * scaleY));
        clipRect.Width = std::min((int)sourceWidth - clipRect.X, (int)((maxX - minX + 1 + pad * 2) * scaleX));
        clipRect.Height = std::min((int)sourceHeight - clipRect.Y, (int)((maxY - minY + 1 + pad * 2) * scaleY));
        if (clipRect.Width <= 0 || clipRect.Height <= 0) {
            break;
        }

        // 2) Kirp ve en-boy oranini koruyarak 128'e sigdir.
        if (FAILED(factory->CreateBitmapClipper(&clipper)) || FAILED(clipper->Initialize(frame, &clipRect))) {
            break;
        }

        const float aspect = (float)clipRect.Width / (float)clipRect.Height;
        UINT fitW = kIconSize;
        UINT fitH = kIconSize;
        if (aspect >= 1.0f) {
            fitH = std::max(1u, (UINT)(kIconSize / aspect));
        } else {
            fitW = std::max(1u, (UINT)(kIconSize * aspect));
        }

        if (FAILED(factory->CreateBitmapScaler(&fit)) ||
            FAILED(fit->Initialize(clipper, fitW, fitH, WICBitmapInterpolationModeFant)) ||
            FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(fit, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                         nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
            break;
        }

        std::vector<unsigned char> pixels((size_t)fitW * fitH * 4);
        if (FAILED(converter->CopyPixels(nullptr, fitW * 4, (UINT)pixels.size(), pixels.data()))) {
            break;
        }

        // 3) Beyaz RGBA doku: renk sabit, alfa = kapsama.
        std::vector<unsigned char> rgba((size_t)kIconSize * kIconSize * 4, 0);
        const int offsetX = ((int)kIconSize - (int)fitW) / 2;
        const int offsetY = ((int)kIconSize - (int)fitH) / 2;
        for (UINT y = 0; y < fitH; ++y) {
            for (UINT x = 0; x < fitW; ++x) {
                const unsigned char alpha = pixels[((size_t)y * fitW + x) * 4 + 3];
                const size_t dst = ((size_t)(y + offsetY) * kIconSize + (x + offsetX)) * 4;
                rgba[dst + 0] = 255;
                rgba[dst + 1] = 255;
                rgba[dst + 2] = 255;
                rgba[dst + 3] = alpha;
            }
        }

        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // Mip zinciri CPU'da kutu filtresiyle uretilir (GL 1.1 uyumlu; uzanti
        // gerekmez).  Kucuk boyutlarda (16-24 px) sirf GL_LINEAR minifikasyon
        // dokuyu seyrelestirip kesik/titrek cizim yapar; mip zinciri onler.
        int levelSize = kIconSize;
        int level = 0;
        std::vector<unsigned char> current = std::move(rgba);
        for (;;) {
            glTexImage2D(GL_TEXTURE_2D, level++, GL_RGBA, levelSize, levelSize, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, current.data());
            if (levelSize == 1) {
                break;
            }
            const int nextSize = std::max(1, levelSize / 2);
            std::vector<unsigned char> next((size_t)nextSize * nextSize * 4, 0);
            for (int y = 0; y < nextSize; ++y) {
                for (int x = 0; x < nextSize; ++x) {
                    unsigned sum = 0;
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(levelSize - 1, x * 2 + dx);
                            const int sy = std::min(levelSize - 1, y * 2 + dy);
                            sum += current[((size_t)sy * levelSize + sx) * 4 + 3];
                        }
                    }
                    const size_t dst = ((size_t)y * nextSize + x) * 4;
                    next[dst + 0] = 255;
                    next[dst + 1] = 255;
                    next[dst + 2] = 255;
                    next[dst + 3] = (unsigned char)(sum / 4);
                }
            }
            current.swap(next);
            levelSize = nextSize;
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    } while (false);

    if (converter != nullptr) {
        converter->Release();
    }
    if (fit != nullptr) {
        fit->Release();
    }
    if (clipper != nullptr) {
        clipper->Release();
    }
    if (probeConverter != nullptr) {
        probeConverter->Release();
    }
    if (probe != nullptr) {
        probe->Release();
    }
    if (frame != nullptr) {
        frame->Release();
    }
    if (decoder != nullptr) {
        decoder->Release();
    }
    stream->Release();
    return texture;
}

// Font Awesome solid glifini kutunun ortasina, tam yuklu boyutta cizer.
// (Glif koordinatlari RenderText'te pos'a gore verilir: y = pos.y + Y0.)
bool drawGlyph(ImDrawList* drawList, std::uint16_t codepointValue, ImVec2 center, float size,
               ImU32 tint) {
    Fonts& f = fonts();
    ImFont* font = nullptr;
    if (size >= 32.0f) {
        font = f.iconBig != nullptr ? f.iconBig : f.iconMid;
    } else if (size >= 20.0f) {
        font = f.iconMid != nullptr ? f.iconMid : f.iconSmall;
    } else {
        font = f.iconSmall != nullptr ? f.iconSmall : f.iconMid;
    }
    if (font == nullptr) {
        return false;
    }

    const ImFontGlyph* glyph = font->FindGlyphNoFallback((ImWchar)codepointValue);
    if (glyph == nullptr || !glyph->Visible) {
        return false;
    }

    // FA kod noktalari > 0xE000: 3 baytlik UTF-8.
    char utf8[4] = {};
    utf8[0] = (char)(0xE0 | (codepointValue >> 12));
    utf8[1] = (char)(0x80 | ((codepointValue >> 6) & 0x3F));
    utf8[2] = (char)(0x80 | (codepointValue & 0x3F));

    const ImVec2 pos(center.x - (glyph->X0 + glyph->X1) * 0.5f,
                     center.y - (glyph->Y0 + glyph->Y1) * 0.5f);
    drawList->AddText(font, font->FontSize, pos, tint, utf8);
    return true;
}

} // namespace

void initialize(const char* /*hint*/) {
    if (!textures().empty()) {
        return;
    }

    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        g_comInitialized = true;
    }

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        return;
    }

    // DK-PORT: iterate the embedded blob table (was: directory_iterator over
    // resource\icons, files renamed "icon_<ad>.png").
    for (unsigned int i = 0; i < arif_icon_blob_count; ++i) {
        const ArifIconBlob& blob = arif_icon_blobs[i];
        if (blob.name == nullptr || blob.bytes == nullptr || blob.size == 0) {
            continue;
        }
        const GLuint texture = loadIcon(factory, blob.bytes, blob.size);
        if (texture != 0) {
            textures()[blob.name] = texture;
        }
    }

    factory->Release();
}

void forget() {
    // DK-PORT: see IconPack.hpp - bookkeeping only, no GL calls.
    textures().clear();

    if (g_comInitialized) {
        CoUninitialize();
        g_comInitialized = false;
    }
}

void shutdown() {
    for (auto& pair : textures()) {
        if (pair.second != 0) {
            glDeleteTextures(1, &pair.second);
            pair.second = 0;
        }
    }
    textures().clear();

    if (g_comInitialized) {
        CoUninitialize();
        g_comInitialized = false;
    }
}

bool has(const char* name) {
    // PNG paketi (feather raster) oncelikli; FA solid glifi yedek.
    if (name == nullptr) {
        return false;
    }
    const auto it = textures().find(name);
    if (it != textures().end() && it->second != 0) {
        return true;
    }
    if (fa::codepoint(name) != 0) {
        return true;
    }
    return false;
}

bool draw(ImDrawList* drawList, const char* name, ImVec2 min, ImVec2 max, ImU32 tint) {
    if (drawList == nullptr || name == nullptr) {
        return false;
    }
    // 1) PNG paketi (embedded feather raster) — oncelikli.
    const auto it = textures().find(name);
    if (it != textures().end() && it->second != 0) {
        drawList->AddImage((ImTextureID)(intptr_t)it->second, min, max, ImVec2(0.0f, 0.0f),
                           ImVec2(1.0f, 1.0f), tint);
        return true;
    }
    // 2) Font Awesome (Solid) glifi — yedek.
    if (const std::uint16_t cp = fa::codepoint(name)) {
        return drawGlyph(drawList, cp, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
                         max.x - min.x, tint);
    }
    return false;
}

bool drawCentered(ImDrawList* drawList, const char* name, ImVec2 center, float size, ImU32 tint) {
    const float half = size * 0.5f;
    return draw(drawList, name, ImVec2(center.x - half, center.y - half),
                ImVec2(center.x + half, center.y + half), tint);
}

bool drawRotated(ImDrawList* drawList, const char* name, ImVec2 center, float size, float angleRadians,
                 ImU32 tint) {
    if (drawList == nullptr || name == nullptr) {
        return false;
    }
    // PNG paketi (feather raster) oncelikli.
    const auto it = textures().find(name);
    if (it != textures().end() && it->second != 0) {
        const float half = size * 0.5f;
        const float c = std::cos(angleRadians);
        const float s = std::sin(angleRadians);
        const ImVec2 corners[4] = {{-half, -half}, {half, -half}, {half, half}, {-half, half}};
        ImVec2 positions[4];
        for (int i = 0; i < 4; ++i) {
            positions[i] = ImVec2(center.x + corners[i].x * c - corners[i].y * s,
                                  center.y + corners[i].x * s + corners[i].y * c);
        }
        drawList->AddImageQuad((ImTextureID)(intptr_t)it->second, positions[0], positions[1],
                               positions[2], positions[3], ImVec2(0.0f, 0.0f), ImVec2(1.0f, 0.0f),
                               ImVec2(1.0f, 1.0f), ImVec2(0.0f, 1.0f), tint);
        return true;
    }
    // FA glifi dondurulmeden cizilir (refresh donmesi vektorel kaliyor).
    if (const std::uint16_t cp = fa::codepoint(name)) {
        return drawGlyph(drawList, cp, center, size, tint);
    }
    return false;
}

const char* moduleIconName(const char* moduleId) {
    if (moduleId == nullptr) {
        return nullptr;
    }
    // DK-PORT: the client's module names are mixed case ("KillAura"); pack
    // keys are lowercase file stems. Lowercase into a static buffer so both
    // the PNG lookup and the FA fallback see "killaura".
    static char lower[96];
    size_t n = 0;
    for (; moduleId[n] != '\0' && n + 1 < sizeof(lower); ++n) {
        const char c = moduleId[n];
        lower[n] = (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
    }
    lower[n] = '\0';

    if (std::strcmp(lower, "search") == 0) {
        return "search_grid";
    }
    return lower;
}

} // namespace arif::gui::iconpack
