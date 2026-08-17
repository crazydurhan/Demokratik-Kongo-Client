#include "MovingObjectPosition.h"
#include "../entity/Entity.h"

#include "../../../../java/java.h"
#include "../../../strayCache.h"
#include "../../../jniResolve.h"

#include <string>

namespace
{
	// MovingObjectType is compared on every crosshair/friend check. Resolving the
	// enum constants once and pinning them as globals removes GetObjectClass +
	// GetStaticFieldID + GetStaticObjectField from the per-call path, and - more
	// importantly - a failed lookup used to return with a pending NoSuchFieldError
	// that poisoned every later JNI call on this thread.
	jobject g_typeBlock = nullptr;
	jobject g_typeEntity = nullptr;
	bool    g_typeResolveFailed = false;

	bool EnsureMovingObjectTypes(JNIEnv* env, jobject typeOfHit)
	{
		if (g_typeBlock && g_typeEntity)
			return true;
		if (g_typeResolveFailed || !env || !typeOfHit)
			return false;

		jclass enumClass = env->GetObjectClass(typeOfHit);
		if (!enumClass)
		{
			JniResolve::ClearException(env);
			g_typeResolveFailed = true;
			return false;
		}

		// Derive the descriptor from the runtime class so obfuscated clients
		// (where the enum is not net/minecraft/util/...$MovingObjectType) work too.
		const std::string sig = JniResolve::ClassToDescriptor(env, enumClass);
		if (sig.empty())
		{
			JniResolve::ClearException(env);
			env->DeleteLocalRef(enumClass);
			g_typeResolveFailed = true;
			return false;
		}

		jfieldID blockFid = env->GetStaticFieldID(enumClass, "BLOCK", sig.c_str());
		JniResolve::ClearException(env);
		jfieldID entityFid = env->GetStaticFieldID(enumClass, "ENTITY", sig.c_str());
		JniResolve::ClearException(env);

		if (blockFid)
		{
			if (jobject local = env->GetStaticObjectField(enumClass, blockFid))
			{
				g_typeBlock = env->NewGlobalRef(local);
				env->DeleteLocalRef(local);
			}
			JniResolve::ClearException(env);
		}
		if (entityFid)
		{
			if (jobject local = env->GetStaticObjectField(enumClass, entityFid))
			{
				g_typeEntity = env->NewGlobalRef(local);
				env->DeleteLocalRef(local);
			}
			JniResolve::ClearException(env);
		}

		env->DeleteLocalRef(enumClass);

		if (!g_typeBlock || !g_typeEntity)
		{
			g_typeResolveFailed = true;
			return false;
		}
		return true;
	}

	// Shared body for IsTypeOfBlock / IsTypeOfEntity.
	bool MatchesType(jobject instance, bool wantBlock)
	{
		JNIEnv* env = Java::GetEnv();
		if (!env || !instance || !StrayCache::movingObjectPosition_typeOfHit)
			return false;

		jobject typeOfHit = env->GetObjectField(instance, StrayCache::movingObjectPosition_typeOfHit);
		JniResolve::ClearException(env);
		if (!typeOfHit)
			return false;

		bool isSame = false;
		if (EnsureMovingObjectTypes(env, typeOfHit))
		{
			jobject wanted = wantBlock ? g_typeBlock : g_typeEntity;
			isSame = env->IsSameObject(wanted, typeOfHit);
			JniResolve::ClearException(env);
		}

		env->DeleteLocalRef(typeOfHit);
		return isSame;
	}
}

CMovingObjectPosition::CMovingObjectPosition()
{
	if (!StrayCache::initialized) StrayCache::Initialize();
	this->Class = StrayCache::movingObjectPosition_class;
}

CMovingObjectPosition::CMovingObjectPosition(jobject instance) : CMovingObjectPosition()
{
	this->Instance = instance;
}

jclass CMovingObjectPosition::GetClass()
{
	return this->Class;
}

jobject CMovingObjectPosition::GetInstance()
{
	return this->Instance;
}

CVec3 CMovingObjectPosition::GetBlockPosition()
{
	JNIEnv* env = Java::GetEnv();
	if (!env || !this->GetInstance() || !StrayCache::movingObjectPosition_hitVec)
		return CVec3(nullptr);

	jobject hitVec = env->GetObjectField(this->GetInstance(), StrayCache::movingObjectPosition_hitVec);
	JniResolve::ClearException(env);
	CVec3 result(hitVec);
	if (hitVec) env->DeleteLocalRef(hitVec);
	return result;
}

bool CMovingObjectPosition::IsTypeOfBlock()
{
	return MatchesType(this->GetInstance(), /*wantBlock=*/true);
}

bool CMovingObjectPosition::IsTypeOfEntity()
{
	return MatchesType(this->GetInstance(), /*wantBlock=*/false);
}

CEntity CMovingObjectPosition::GetEntity()
{
	JNIEnv* env = Java::GetEnv();
	if (!env || !this->GetInstance() || !StrayCache::movingObjectPosition_entityHit)
		return CEntity(nullptr);

	jobject entityHit = env->GetObjectField(this->GetInstance(), StrayCache::movingObjectPosition_entityHit);
	JniResolve::ClearException(env);
	CEntity result(entityHit);
	if (entityHit) env->DeleteLocalRef(entityHit);
	return result;
}
