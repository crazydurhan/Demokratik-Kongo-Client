#pragma once

// Frame-rate independent motion primitives shared by the launcher and the
// in-game ClickGUI. Everything here is header-only and depends on nothing but
// imgui.h, so both binaries get byte-identical easing behaviour.
//
// All rates are expressed "per second": Ease(value, target, 15.0f) closes
// ~63% of the remaining distance every 1/15 s regardless of frame rate.

#include "imgui.h"

#include <cmath>

namespace dk::ui {

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline float SmoothStep(float v)
{
    v = Clamp01(v);
    return v * v * (3.0f - 2.0f * v);
}

// io.DeltaTime clamped so an alt-tab or loading stall never makes an eased
// value jump past its target (or a spring explode).
inline float DeltaTime()
{
    float dt = ImGui::GetIO().DeltaTime;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    return dt;
}

// 1.0 normally. The ClickGUI raises this hard when "reduce motion" is on so
// every easing call lands on its target within a single frame.
inline float& MotionMultiplier()
{
    static float value = 1.0f;
    return value;
}

inline float EaseRate(float speed)
{
    return 1.0f - std::exp(-speed * MotionMultiplier() * DeltaTime());
}

inline void Ease(float& value, float target, float speed)
{
    value += (target - value) * EaseRate(speed);
}

inline void Ease(ImVec4& value, const ImVec4& target, float speed)
{
    const float t = EaseRate(speed);
    value.x += (target.x - value.x) * t;
    value.y += (target.y - value.y) * t;
    value.z += (target.z - value.z) * t;
    value.w += (target.w - value.w) * t;
}

// Damped spring. Gives toggles and knobs their overshoot instead of a flat lerp.
enum class SpringStyle { Fast, Critical, Bouncy, Overdamped };

struct Spring
{
    float x = 0.0f, v = 0.0f, target = 0.0f;
    float stiffness = 280.0f, damping = 0.0f, mass = 1.0f;

    static Spring Make(SpringStyle style, float start = 0.0f)
    {
        Spring sp;
        sp.x = start;
        sp.target = start;
        switch (style) {
        case SpringStyle::Fast:       sp.stiffness = 450.0f; sp.damping = 2.00f * std::sqrt(sp.stiffness * sp.mass); break;
        case SpringStyle::Critical:   sp.stiffness = 280.0f; sp.damping = 2.00f * std::sqrt(sp.stiffness * sp.mass); break;
        case SpringStyle::Bouncy:     sp.stiffness = 320.0f; sp.damping = 1.10f * std::sqrt(sp.stiffness * sp.mass); break;
        case SpringStyle::Overdamped: sp.stiffness = 220.0f; sp.damping = 3.20f * std::sqrt(sp.stiffness * sp.mass); break;
        }
        return sp;
    }

    void Tick(float dt)
    {
        if (damping <= 0.0f)
            damping = 2.0f * std::sqrt(stiffness * mass);

        // Sub-step below 120 Hz so the integrator stays stable on slow frames.
        const int steps = (dt > 1.0f / 120.0f) ? 2 : 1;
        const float h = dt / static_cast<float>(steps);
        for (int i = 0; i < steps; ++i) {
            const float force = -stiffness * (x - target) - damping * v;
            v += (force / mass) * h;
            x += v * h;
        }
    }

    void Snap(float to) { x = to; v = 0.0f; target = to; }

    // Convenience: retarget + advance by the clamped frame delta, honouring
    // the motion multiplier (reduce-motion snaps instantly).
    void Drive(float newTarget)
    {
        target = newTarget;
        if (MotionMultiplier() > 1.5f) { Snap(newTarget); return; }
        Tick(DeltaTime());
    }
};

} // namespace dk::ui
