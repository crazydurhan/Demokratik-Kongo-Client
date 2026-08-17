#include "noHurtCam.h"

#include "../../../sdk/sdk.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/jniResolve.h"
#include "../../../java/java.h"
#include "../../../util/logger.h"
#include "../../../patcher/patcher.h"

NoHurtCam::NoHurtCam()
    : Module("NoHurtCam", "Disable or scale camera shake effect when taking damage.", Category::Render)
{
    m_intensity = &add<NumberSetting>("Intensity", 0.0f, 0.0f, 100.0f, 5.0f);
    m_intensity->suffix = "%";
    m_intensity->description = "Intensity of camera shake when taking damage (0% = completely disabled).";

    setEnabled(false);
}

void NoHurtCam::onEnable()
{
    Logger::Info("NoHurtCam", "Enabled");
    pushAll();
}

void NoHurtCam::onDisable()
{
    Logger::Info("NoHurtCam", "Disabled");
    pushAll();
}

void NoHurtCam::onTick()
{
    pushAll();

    // 0% intensity is handled cleanly by the hurtCameraEffect bytecode patch.
    // Partial intensity scales hurtTime client-side instead (visual only).
    if (!isEnabled() || m_intensity->value <= 0.0f || !SDK::Minecraft) return;

    JNIEnv* env = Java::GetEnv();
    if (!env || !StrayCache::entityLivingBase_hurtTime) return;

    jobject playerObj = SDK::Minecraft->GetThePlayerObject();
    if (!playerObj) return;

    int hurtTime = env->GetIntField(playerObj, StrayCache::entityLivingBase_hurtTime);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (hurtTime > 0) {
        int scaledHurtTime = (int)(hurtTime * (m_intensity->value / 100.0f));
        env->SetIntField(playerObj, StrayCache::entityLivingBase_hurtTime, scaledHurtTime);
        JniResolve::ClearException(env);
    }

    env->DeleteLocalRef(playerObj);
}

void NoHurtCam::pushAll()
{
    // The hurtCameraEffect patch only does a full cancel, so drive it when the
    // user asked for 0% (full disable) — the primary use case.
    const bool fullCancel = isEnabled() && m_intensity && m_intensity->value <= 0.0f;
    Patcher::put("nohurtcam_enabled", fullCancel ? "true" : "false");
}

