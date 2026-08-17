#pragma once

#include <cstdint>

/*
    PROJECTX :: CORE :: Event Types
    -------------------------------
    All concrete event payloads. They are plain structs (POD-ish) so the
    bus does not need to copy heavy data; the dispatcher passes them by
    reference. Keep these as small as possible.
*/

struct TickEvent
{
    float partialTicks;
};

struct PreRenderEvent
{
    float partialTicks;
};

struct PostRenderEvent
{
    float partialTicks;
};

struct RenderWorldEvent
{
    float partialTicks;
};

struct RenderOverlayEvent
{
    float screenWidth;
    float screenHeight;
};

struct KeyEvent
{
    int   virtualKey;
    bool  pressed;   // true = down, false = up
};

struct MouseClickEvent
{
    int   button;    // 0=left,1=right,2=middle
    bool  pressed;
    float x, y;
};
