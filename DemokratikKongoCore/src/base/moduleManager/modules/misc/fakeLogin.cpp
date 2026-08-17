#include "fakeLogin.h"

#include "../../../menu/menu.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/strayCache.h"

#include <string>

namespace
{
    std::string getCurrentUsername()
    {
        JNIEnv* env = Java::GetEnv();
        if (!env || !StrayCache::EnsureEspBridge()) return "Offline";
        // CallStatic* with a null jmethodID crashes the JVM, not returns null.
        if (!StrayCache::espBridge_class || !StrayCache::espBridge_getSessionUsername) return "Offline";

        jstring nameStr = (jstring)env->CallStaticObjectMethod(StrayCache::espBridge_class, StrayCache::espBridge_getSessionUsername);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return "Offline";
        }
        if (!nameStr)
            return "Offline";

        const char* nameChars = env->GetStringUTFChars(nameStr, nullptr);
        std::string name(nameChars ? nameChars : "Offline");
        if (nameChars)
            env->ReleaseStringUTFChars(nameStr, nameChars);
        env->DeleteLocalRef(nameStr);
        return name;
    }

    void changeUsername(const std::string& newName)
    {
        JNIEnv* env = Java::GetEnv();
        if (!env || !StrayCache::EnsureEspBridge()) return;
        if (!StrayCache::espBridge_class || !StrayCache::espBridge_changeSession) return;

        jstring jNewName = env->NewStringUTF(newName.c_str());
        env->CallStaticVoidMethod(StrayCache::espBridge_class, StrayCache::espBridge_changeSession, jNewName);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(jNewName);
    }
}

void RenderFakeLoginOutsideModule(float dt)
{
    (void)dt;
    Menu::FakeLoginOpen = false;
}

std::string FakeLoginCurrentUsername()
{
    return getCurrentUsername();
}

bool FakeLoginSetUsername(const std::string& newName)
{
    if (newName.empty())
        return false;
    changeUsername(newName);
    return true;
}
