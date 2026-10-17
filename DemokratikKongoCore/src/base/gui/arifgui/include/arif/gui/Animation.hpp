// ArifClient ClickGUI - Animasyon altyapisi.
#pragma once

namespace arif::gui {

// ---------------------------------------------------------------------------
// Easing egirleri (t: 0..1)
// ---------------------------------------------------------------------------
namespace easing {
float linear(float t);
float outCubic(float t);
float inOutCubic(float t);
float outQuint(float t);
float outExpo(float t);
float outBack(float t);
} // namespace easing

// ---------------------------------------------------------------------------
// Frame-rate bagimsiz yumusak gecis.
//
//   value = lerp(value, target, 1 - e^(-speed * dt))
//
// Bu yaklasim std::lerp tabanli sabit adimdan farkli olarak histerezis
// yaratmaz ve 30/60/240 FPS'te ayni hizda gorunur.
// ---------------------------------------------------------------------------
class AnimationState {
public:
    AnimationState() = default;
    explicit AnimationState(float start) : value_(start), target_(start) {}

    void setTarget(float target) { target_ = target; }
    void snap(float value) { value_ = value; target_ = value; }
    void setSpeed(float speed) { speed_ = speed; }

    float target() const { return target_; }
    float value() const { return value_; }

    bool done() const;
    void update(float deltaTime);
    float updateAndGet(float deltaTime) {
        update(deltaTime);
        return value_;
    }

private:
    float value_ = 0.0f;
    float target_ = 0.0f;
    float speed_ = 14.0f;  // 1/sn; ~0.15 sn'de hedefe yaklasir
};

// ---------------------------------------------------------------------------
// 0..1 arasi tek seferlik animasyonlar (bildirim, flash, popup)
// ---------------------------------------------------------------------------
class TimedAnimation {
public:
    void play(float duration = 0.15f);
    void stop();

    float update(float deltaTime);  // 0..1 doner
    bool playing() const { return playing_; }
    float value() const { return value_; }

private:
    float duration_ = 0.15f;
    float elapsed_ = 0.0f;
    float value_ = 0.0f;
    bool playing_ = false;
};

// Ayarlar sayfasindan degistirilebilen global hiz carpani (0.25 - 2.0).
float& animationSpeedScale();

} // namespace arif::gui
