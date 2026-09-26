#include "fullbright.h"

#include "../../../menu/menu.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/net/minecraft/client/settings/GameSettings.h"
#include "../../../sdk/net/minecraft/client/entity/EntityPlayerSP.h"
#include "../../../java/java.h"
#include "../../../sdk/jniResolve.h"
#include "../../../sdk/mappingLoader.h"
#include "../../../util/logger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
    struct PotionCache
    {
        jclass  potionEffectClass = nullptr;
        jclass  potionClass       = nullptr;
        jfieldID nightVisionId    = nullptr;
        jmethodID effectCtor      = nullptr;
        jmethodID effectCtorEx    = nullptr; // (id, duration, amplifier, ambient, showParticles)
        jmethodID addPotionEffect = nullptr;
        jmethodID removePotion    = nullptr;
    };

    PotionCache& potions()
    {
        static PotionCache p;
        return p;
    }

    bool ensurePotion(JNIEnv* env)
    {
        PotionCache& p = potions();
        if (p.addPotionEffect && p.effectCtor && p.nightVisionId)
            return true;
        if (!env) return false;

        if (!p.potionEffectClass)
        {
            jclass local = nullptr;
            if (!Java::AssignClass("net.minecraft.potion.PotionEffect", local))
                return false;
            p.potionEffectClass = local;
        }
        if (!p.potionClass)
        {
            jclass local = nullptr;
            if (!Java::AssignClass("net.minecraft.potion.Potion", local))
                return false;
            p.potionClass = local;
        }
        if (!p.nightVisionId)
        {
            for (const std::string& name : MappingLoader::Names("nightVision"))
            {
                p.nightVisionId = env->GetStaticFieldID(p.potionClass, name.c_str(),
                    "Lnet/minecraft/potion/Potion;");
                if (env->ExceptionCheck()) { env->ExceptionClear(); p.nightVisionId = nullptr; continue; }
                if (p.nightVisionId) break;
            }
        }
        if (!p.effectCtor)
            p.effectCtor = env->GetMethodID(p.potionEffectClass, "<init>", "(III)V");
        if (!p.effectCtorEx)
            p.effectCtorEx = env->GetMethodID(p.potionEffectClass, "<init>", "(IIZZ)V");
        if (!p.addPotionEffect)
        {
            jclass living = nullptr;
            if (!Java::AssignClass("net.minecraft.entity.EntityLivingBase", living))
                return false;
            p.addPotionEffect = JniResolve::Method(env, living,
                "(Lnet/minecraft/potion/PotionEffect;)V", "addPotionEffect");
            p.removePotion = JniResolve::Method(env, living, "(I)V", "removePotionEffect");
        }

        return p.addPotionEffect && p.effectCtor && p.nightVisionId;
    }

    inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
}

Fullbright::Fullbright()
    : Module("Fullbright", "Remove darkness via gamma or night vision.", Category::Render)
{
    m_mode = &add<EnumSetting>("Mode",
        std::vector<const char*>{ "Night Vision", "Gamma", "Fade" }, 1);

    m_gammaStrength = &add<NumberSetting>("Gamma Strength", 100.0f, 1.0f, 100.0f, 1.0f);
    m_gammaStrength->suffix = "%";
    m_gammaStrength->description = "Gamma applied in Gamma mode (100 = full brightness).";
    m_gammaStrength->visible = [this] { return m_mode->index == 1; };

    m_fadeMin = &add<NumberSetting>("Fade Min Gamma", 1.0f, 0.1f, 10.0f, 0.1f);
    m_fadeMin->suffix = "%";
    m_fadeMin->description = "Gamma in fully lit areas.";
    m_fadeMin->visible = [this] { return m_mode->index == 2; };

    m_fadeMax = &add<NumberSetting>("Fade Max Gamma", 100.0f, 1.0f, 100.0f, 1.0f);
    m_fadeMax->suffix = "%";
    m_fadeMax->description = "Gamma in total darkness.";
    m_fadeMax->visible = [this] { return m_mode->index == 2; };

    m_fadeSpeed = &add<NumberSetting>("Fade Speed", 0.18f, 0.01f, 1.0f, 0.01f);
    m_fadeSpeed->description = "How fast gamma transitions (1 = instant).";
    m_fadeSpeed->visible = [this] { return m_mode->index == 2; };

    m_hideParticles = &add<BoolSetting>("Hide NV Particles", true);
    m_hideParticles->description = "Suppress night-vision particle effects (PotFx particles).";
    m_hideParticles->visible = [this] { return m_mode->index == 0; };

    setEnabled(false);
}

void Fullbright::onEnable()
{
    Logger::Info("Fullbright", "Enabled");
    applyMode();
}

void Fullbright::onDisable()
{
    revertMode();
    Logger::Info("Fullbright", "Disabled");
}

void Fullbright::onTick()
{
    if (!isEnabled() || Menu::Open)
        return;

    if (!SDK::Minecraft || !SDK::Minecraft->IsReady() || !SDK::Minecraft->gameSettings)
        return;

    switch (m_mode->index)
    {
    case 0: // Night Vision
        applyNightVision(true);
        break;
    case 1: // Gamma
        SDK::Minecraft->gameSettings->SetGamma(m_gammaStrength->value);
        break;
    case 2: // Fade
    {
        const float light = readBlockLight();
        const float lo = m_fadeMin->value;
        const float hi = std::max(lo, m_fadeMax->value);
        const float target = lo + (hi - lo) * (1.0f - clamp01(light / 15.0f));
        const float k = clamp01(m_fadeSpeed->value);
        m_fadeGamma += (target - m_fadeGamma) * k;
        SDK::Minecraft->gameSettings->SetGamma(m_fadeGamma);
        break;
    }
    default:
        break;
    }
}

std::string Fullbright::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None)
        return "";

    const char* modeName =
        m_mode->index == 0 ? "NV" :
        m_mode->index == 1 ? "Gamma" : "Fade";

    if (detail == SuffixDetail::Basic)
        return modeName;

    char buf[96];
    if (m_mode->index == 1)
        std::snprintf(buf, sizeof(buf), "%s %.0f%%", modeName, m_gammaStrength->value);
    else if (m_mode->index == 2)
        std::snprintf(buf, sizeof(buf), "%s %.0f-%.0f%% x%.2f",
            modeName, m_fadeMin->value, m_fadeMax->value, m_fadeSpeed->value);
    else
        std::snprintf(buf, sizeof(buf), "%s%s", modeName,
            m_hideParticles->value ? " [No Particles]" : "");
    return buf;
}

void Fullbright::applyMode()
{
    if (!SDK::Minecraft || !SDK::Minecraft->gameSettings)
        return;

    m_savedGamma = SDK::Minecraft->gameSettings->GetGamma();
    if (m_savedGamma > 10.0f)
        m_savedGamma = 1.0f;
    m_fadeGamma = m_savedGamma;

    switch (m_mode->index)
    {
    case 0:
        applyNightVision(true);
        break;
    case 1:
        SDK::Minecraft->gameSettings->SetGamma(m_gammaStrength->value);
        break;
    case 2:
        break;
    default:
        break;
    }
}

void Fullbright::revertMode()
{
    applyNightVision(false);

    if (SDK::Minecraft && SDK::Minecraft->gameSettings)
    {
        float restore = m_savedGamma;
        if (restore > 10.0f)
            restore = 1.0f;
        SDK::Minecraft->gameSettings->SetGamma(restore);
    }
}

float Fullbright::readBlockLight()
{
    JNIEnv* env = Java::GetEnv();
    if (!env || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return 8.0f;

    jobject player = SDK::Minecraft->GetThePlayerObject();
    jobject world = SDK::Minecraft->GetTheWorldObject();
    if (!player || !world)
    {
        if (player) env->DeleteLocalRef(player);
        if (world) env->DeleteLocalRef(world);
        return 8.0f;
    }

    static jmethodID getBrightness = nullptr;
    if (!getBrightness)
    {
        jclass entityCls = env->GetObjectClass(player);
        if (entityCls)
        {
            getBrightness = JniResolve::Method(env, entityCls, "(F)F", "getBrightnessForRender");
            env->DeleteLocalRef(entityCls);
        }
    }

    float brightness = 0.5f;
    if (getBrightness)
    {
        brightness = env->CallFloatMethod(player, getBrightness, 1.0f);
        JniResolve::ClearException(env);
        brightness *= 15.0f;
    }

    env->DeleteLocalRef(player);
    env->DeleteLocalRef(world);
    return brightness;
}

void Fullbright::applyNightVision(bool enable)
{
    if (enable && m_nvActive) return;
    if (!enable && !m_nvActive) return;

    JNIEnv* env = Java::GetEnv();
    if (!env || !ensurePotion(env) || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return;

    jobject player = SDK::Minecraft->GetThePlayerObject();
    if (!player) return;

    PotionCache& p = potions();

    static jmethodID getId = nullptr;
    if (!getId)
        getId = JniResolve::Method(env, p.potionClass, "()I", "getId");

    if (enable)
    {
        jint id = 16;
        jobject potion = env->GetStaticObjectField(p.potionClass, p.nightVisionId);
        if (potion && getId)
        {
            id = env->CallIntMethod(potion, getId);
            JniResolve::ClearException(env);
            env->DeleteLocalRef(potion);
        }

        // Use the extended ctor when hiding particles: (id, duration, amplifier, ambient=false, showParticles=false)
        jobject effect = nullptr;
        if (m_hideParticles->value && p.effectCtorEx)
        {
            effect = env->NewObject(p.potionEffectClass, p.effectCtorEx,
                id, 9999, 0, JNI_FALSE, JNI_FALSE);
        }
        else
        {
            effect = env->NewObject(p.potionEffectClass, p.effectCtor, id, 9999, 0);
        }
        if (effect)
        {
            env->CallVoidMethod(player, p.addPotionEffect, effect);
            JniResolve::ClearException(env);
            env->DeleteLocalRef(effect);
        }
        m_nvActive = true;
    }
    else
    {
        jint id = 16;
        jobject potion = env->GetStaticObjectField(p.potionClass, p.nightVisionId);
        if (potion && getId)
        {
            id = env->CallIntMethod(potion, getId);
            JniResolve::ClearException(env);
            env->DeleteLocalRef(potion);
        }

        if (p.removePotion)
        {
            env->CallVoidMethod(player, p.removePotion, id);
            JniResolve::ClearException(env);
        }
        m_nvActive = false;
    }

    env->DeleteLocalRef(player);
}
