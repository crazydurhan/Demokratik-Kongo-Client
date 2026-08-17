#include "Timer.h"

#include "../../../../java/java.h"
#include "../../../sdk.h"
#include "../../../jniResolve.h"

CTimer::CTimer()
{
	JNIEnv* env = Java::GetEnv();
	if (!Java::AssignClass("net.minecraft.util.Timer", this->Class) || !env)
		return;

	this->FieldIDs["renderPartialTicks"] = JniResolve::Field(env, this->Class, "F", "renderPartialTicks");
}

float CTimer::GetRenderPartialTicks()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	jfieldID fid = this->FieldIDs["renderPartialTicks"];
	if (!env || !inst || !fid) return 0.0f;
	float v = env->GetFloatField(inst, fid);
	JniResolve::ClearException(env);
	return v;
}

jclass CTimer::GetClass()
{
	return this->Class;
}

jobject CTimer::GetInstance()
{
	if (!SDK::Minecraft) return nullptr;
	jobject mcInstance = SDK::Minecraft->GetInstance();
	jfieldID fid = SDK::Minecraft->FieldIDs["timer"];
	JNIEnv* env = Java::GetEnv();
	if (!env || !mcInstance || !fid) return nullptr;
	jobject timer = env->GetObjectField(mcInstance, fid);
	JniResolve::ClearException(env);
	return timer;
}
