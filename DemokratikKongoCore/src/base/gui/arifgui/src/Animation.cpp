#include "arif/gui/Animation.hpp"

#include <algorithm>
#include <cmath>

namespace arif::gui {

namespace easing {

float linear(float t) { return t; }

float outCubic(float t) {
    const float u = 1.0f - std::min(1.0f, std::max(0.0f, t));
    return 1.0f - u * u * u;
}

float inOutCubic(float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    return (t < 0.5f) ? (4.0f * t * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f);
}

float outQuint(float t) {
    const float u = 1.0f - std::min(1.0f, std::max(0.0f, t));
    return 1.0f - u * u * u * u * u;
}

float outExpo(float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    return (t >= 1.0f) ? 1.0f : (1.0f - std::pow(2.0f, -10.0f * t));
}

float outBack(float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

} // namespace easing

// ---------------------------------------------------------------------------
// AnimationState
// ---------------------------------------------------------------------------
bool AnimationState::done() const {
    return std::fabs(target_ - value_) < 0.0005f;
}

void AnimationState::update(float deltaTime) {
    const float speed = speed_ * animationSpeedScale();
    const float k = 1.0f - std::exp(-speed * deltaTime);
    value_ += (target_ - value_) * k;
    if (std::fabs(target_ - value_) < 0.0005f) {
        value_ = target_;
    }
}

// ---------------------------------------------------------------------------
// TimedAnimation
// ---------------------------------------------------------------------------
void TimedAnimation::play(float duration) {
    duration_ = std::max(0.01f, duration);
    elapsed_ = 0.0f;
    value_ = 0.0f;
    playing_ = true;
}

void TimedAnimation::stop() {
    playing_ = false;
    value_ = 0.0f;
}

float TimedAnimation::update(float deltaTime) {
    if (!playing_) {
        return value_;
    }
    elapsed_ += deltaTime;
    const float t = std::min(1.0f, elapsed_ / duration_);
    value_ = easing::outCubic(t);
    if (t >= 1.0f) {
        playing_ = false;
    }
    return value_;
}

// ---------------------------------------------------------------------------
// Global animasyon hizi
// ---------------------------------------------------------------------------
float& animationSpeedScale() {
    static float scale = 1.0f;
    return scale;
}

} // namespace arif::gui
