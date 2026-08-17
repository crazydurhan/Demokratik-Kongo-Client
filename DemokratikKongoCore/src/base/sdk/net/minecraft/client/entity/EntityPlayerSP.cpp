#include "EntityPlayerSP.h"
#include "../../../../sdk.h"
#include "../../../../../java/java.h"
#include "../../../../strayCache.h"
#include "../../../../jniResolve.h"

#include "../../../../../util/logger.h"

CEntityPlayerSP::CEntityPlayerSP() : CEntityPlayer()
{
	Java::AssignClass("net.minecraft.client.entity.EntityPlayerSP", this->Class);
}

jclass CEntityPlayerSP::GetClass()
{
	return this->Class;
}

jobject CEntityPlayerSP::GetInstance()
{
	if (!SDK::Minecraft) return nullptr;
	jobject mcInstance = SDK::Minecraft->GetInstance();
	if (!mcInstance) return nullptr;
	return Java::Env->GetObjectField(mcInstance, SDK::Minecraft->FieldIDs["thePlayer"]);
}

void CEntityPlayerSP::SendChatMessage(const std::string& text)
{
	if (text.empty() || !StrayCache::entityPlayerSP_sendChatMessage)
		return;

	JNIEnv* env = Java::GetEnv();
	if (!env) return;

	jobject player = GetInstance();
	if (!player) return;

	jstring jText = env->NewStringUTF(text.c_str());
	if (!jText) {
		env->DeleteLocalRef(player);
		return;
	}

	env->CallVoidMethod(player, StrayCache::entityPlayerSP_sendChatMessage, jText);
	JniResolve::ClearException(env);

	env->DeleteLocalRef(jText);
	env->DeleteLocalRef(player);
}
