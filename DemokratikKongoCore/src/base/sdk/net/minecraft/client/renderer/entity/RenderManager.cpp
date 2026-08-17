#include "RenderManager.h"

#include "../../../../../../java/java.h"
#include "../../../../../sdk.h"
#include "../../../../../jniResolve.h"

CRenderManager::CRenderManager()
{
	JNIEnv* env = Java::GetEnv();
	if (StrayCache::renderManager_class)
		this->Class = StrayCache::renderManager_class;
	else if (!Java::AssignClass("net.minecraft.client.renderer.entity.RenderManager", this->Class) || !env)
		return;

	this->FieldIDs["renderPosX"] = StrayCache::renderManager_renderPosX ? StrayCache::renderManager_renderPosX : JniResolve::Field(env, this->Class, "D", "renderPosX");
	this->FieldIDs["renderPosY"] = StrayCache::renderManager_renderPosY ? StrayCache::renderManager_renderPosY : JniResolve::Field(env, this->Class, "D", "renderPosY");
	this->FieldIDs["renderPosZ"] = StrayCache::renderManager_renderPosZ ? StrayCache::renderManager_renderPosZ : JniResolve::Field(env, this->Class, "D", "renderPosZ");
	this->FieldIDs["viewerPosX"] = JniResolve::Field(env, this->Class, "D", "viewerPosX");
	this->FieldIDs["viewerPosY"] = JniResolve::Field(env, this->Class, "D", "viewerPosY");
	this->FieldIDs["viewerPosZ"] = JniResolve::Field(env, this->Class, "D", "viewerPosZ");
}

Vector3 CRenderManager::RenderPos()
{
	JNIEnv* env = Java::GetEnv();
	jobject instance = this->GetInstance();
	if (!env || !instance)
		return Vector3{};

	jfieldID fx = this->FieldIDs["renderPosX"];
	jfieldID fy = this->FieldIDs["renderPosY"];
	jfieldID fz = this->FieldIDs["renderPosZ"];
	if (!fx || !fy || !fz)
	{
		env->DeleteLocalRef(instance);
		return Vector3{};
	}

	Vector3 pos{
		(float)(double)env->GetDoubleField(instance, fx),
		(float)(double)env->GetDoubleField(instance, fy),
		(float)(double)env->GetDoubleField(instance, fz)
	};
	JniResolve::ClearException(env);
	env->DeleteLocalRef(instance);
	return pos;
}

Vector3 CRenderManager::ViewerPos()
{
	JNIEnv* env = Java::GetEnv();
	jobject instance = this->GetInstance();
	if (!env || !instance)
		return Vector3{};

	jfieldID fx = this->FieldIDs["viewerPosX"];
	jfieldID fy = this->FieldIDs["viewerPosY"];
	jfieldID fz = this->FieldIDs["viewerPosZ"];
	if (!fx || !fy || !fz)
	{
		env->DeleteLocalRef(instance);
		return Vector3{};
	}

	Vector3 pos{
		(float)(double)env->GetDoubleField(instance, fx),
		(float)(double)env->GetDoubleField(instance, fy),
		(float)(double)env->GetDoubleField(instance, fz)
	};
	JniResolve::ClearException(env);
	env->DeleteLocalRef(instance);
	return pos;
}

jclass CRenderManager::GetClass()
{
	return this->Class;
}

jobject CRenderManager::GetInstance()
{
	if (!SDK::Minecraft) return nullptr;
	jobject mcInstance = SDK::Minecraft->GetInstance();
	jfieldID fid = SDK::Minecraft->FieldIDs["renderManager"];
	JNIEnv* env = Java::GetEnv();
	if (!env || !mcInstance || !fid) return nullptr;
	jobject rm = env->GetObjectField(mcInstance, fid);
	JniResolve::ClearException(env);
	return rm;
}
