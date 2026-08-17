#include "GameSettings.h"
#include "../../../../sdk.h"
#include "../../../../../java/java.h"
#include "../../../../jniResolve.h"

CGameSettings::CGameSettings()
{
	JNIEnv* env = Java::GetEnv();
	if (!Java::AssignClass("net.minecraft.client.settings.GameSettings", this->Class) || !env)
		return;

	this->FieldIDs["thirdPersonView"] = JniResolve::Field(env, this->Class, "I", "thirdPersonView");
	this->FieldIDs["fovSetting"] = JniResolve::Field(env, this->Class, "F", "fovSetting");
	this->FieldIDs["gammaSetting"] = JniResolve::Field(env, this->Class, "F", "gammaSetting");
	this->FieldIDs["mouseSensitivity"] = JniResolve::Field(env, this->Class, "F", "mouseSensitivity");
	if (!this->FieldIDs["mouseSensitivity"]) {
		this->FieldIDs["mouseSensitivity"] = JniResolve::Field(env, this->Class, "F", "field_74341_c");
	}
	this->FieldIDs["keyBindFullscreen"] = JniResolve::Field(env, this->Class, "Lnet/minecraft/client/settings/KeyBinding;", "keyBindFullscreen");
	this->MethodIDs["setOptionKeyBinding"] = JniResolve::Method(env, this->Class, "(Lnet/minecraft/client/settings/KeyBinding;I)V", "setOptionKeyBinding");
}

jclass CGameSettings::GetClass()
{
	return this->Class;
}

jobject CGameSettings::GetInstance()
{
	if (!SDK::Minecraft) return nullptr;
	jobject mcInstance = SDK::Minecraft->GetInstance();
	jfieldID fid = SDK::Minecraft->FieldIDs["gameSettings"];
	JNIEnv* env = Java::GetEnv();
	if (!env || !mcInstance || !fid) return nullptr;
	jobject gs = env->GetObjectField(mcInstance, fid);
	JniResolve::ClearException(env);
	return gs;
}

float CGameSettings::GetFOV()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	jfieldID fid = this->FieldIDs["fovSetting"];
	if (!env || !inst || !fid) return 70.0f;
	float fov = env->GetFloatField(inst, fid);
	JniResolve::ClearException(env);
	env->DeleteLocalRef(inst);
	return fov;
}

float CGameSettings::GetGamma()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	jfieldID fid = this->FieldIDs["gammaSetting"];
	if (!env || !inst || !fid) return 1.0f;
	float gamma = env->GetFloatField(inst, fid);
	JniResolve::ClearException(env);
	env->DeleteLocalRef(inst);
	return gamma;
}

void CGameSettings::SetGamma(float value)
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	jfieldID fid = this->FieldIDs["gammaSetting"];
	if (!env || !inst || !fid) return;
	env->SetFloatField(inst, fid, value);
	JniResolve::ClearException(env);
	env->DeleteLocalRef(inst);
}

void CGameSettings::SetFOV(float value)
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	jfieldID fid = this->FieldIDs["fovSetting"];
	if (!env || !inst || !fid) return;
	env->SetFloatField(inst, fid, value);
	JniResolve::ClearException(env);
	env->DeleteLocalRef(inst);
}

void CGameSettings::SetThirdPersonView(int view)
{
	if (view < 0) view = 0;
	if (view > 2) view = 2;
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	jfieldID fid = this->FieldIDs["thirdPersonView"];
	if (!env || !inst || !fid) return;
	env->SetIntField(inst, fid, view);
	JniResolve::ClearException(env);
	env->DeleteLocalRef(inst);
}

void CGameSettings::SetFullscreenKeyToNull()
{
	JNIEnv* env = Java::GetEnv();
	jobject instance = this->GetInstance();
	jmethodID mid = this->MethodIDs["setOptionKeyBinding"];
	jfieldID keyField = this->FieldIDs["keyBindFullscreen"];
	if (!env || !instance || !mid || !keyField) return;
	jobject key = env->GetObjectField(instance, keyField);
	env->CallVoidMethod(instance, mid, key, 0);
	JniResolve::ClearException(env);
	if (key) env->DeleteLocalRef(key);
}

void CGameSettings::RestoreFullscreenKey()
{
	JNIEnv* env = Java::GetEnv();
	jobject instance = this->GetInstance();
	jmethodID mid = this->MethodIDs["setOptionKeyBinding"];
	jfieldID keyField = this->FieldIDs["keyBindFullscreen"];
	if (!env || !instance || !mid || !keyField) return;
	jobject key = env->GetObjectField(instance, keyField);
	env->CallVoidMethod(instance, mid, key, 87);
	JniResolve::ClearException(env);
	if (key) env->DeleteLocalRef(key);
}

int CGameSettings::GetThirdPersonView()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	jfieldID fid = this->FieldIDs["thirdPersonView"];
	if (!env || !inst || !fid) return 0;
	int v = env->GetIntField(inst, fid);
	JniResolve::ClearException(env);
	return v;
}

float CGameSettings::GetMouseSensitivity()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	jfieldID fid = this->FieldIDs["mouseSensitivity"];
	if (!env || !inst || !fid) return 0.5f;
	float sens = env->GetFloatField(inst, fid);
	JniResolve::ClearException(env);
	env->DeleteLocalRef(inst);
	return sens;
}
