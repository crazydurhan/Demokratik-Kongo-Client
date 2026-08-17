#include "Entity.h"

#include "../../../../java/java.h"

#include "../../../java/lang/String.h"

#include "../../../strayCache.h"

#include "../../../jniResolve.h"



#include "../../../../util/logger.h"

#include <Windows.h>



CEntity::CEntity()

{

	if (!StrayCache::initialized) StrayCache::Initialize();

	this->Class = StrayCache::entity_class;

}



CEntity::CEntity(jobject instance) : CEntity()

{

	this->Instance = instance;

}



jclass CEntity::GetClass()

{

	return this->Class;

}



jobject CEntity::GetInstance()

{

	return this->Instance;

}



std::string CEntity::GetName()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_getName)

		return {};



	jobject nameObj = env->CallObjectMethod(inst, StrayCache::entity_getName);

	JniResolve::ClearException(env);

	if (!nameObj)

		return {};



	String str = String(nameObj);

	env->DeleteLocalRef(nameObj);

	return str.ToString();

}



Vector3 CEntity::GetPos()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_posX || !StrayCache::entity_posY || !StrayCache::entity_posZ)

		return Vector3{};



	return Vector3{

		(float)(double)env->GetDoubleField(inst, StrayCache::entity_posX),

		(float)(double)env->GetDoubleField(inst, StrayCache::entity_posY),

		(float)(double)env->GetDoubleField(inst, StrayCache::entity_posZ)

	};

}



Vector3 CEntity::GetEyePos()

{

	Vector3 pos = GetPos();

	// 1.8 eye height is height * 0.9 (1.62 standing), dropping to 1.54 while
	// sneaking. The old 0.85 factor produced 1.53 - i.e. the sneak height even
	// when standing - which biased every aim/raycast ~9cm low.

	float eyeHeight = this->GetHeight() * 0.9f;

	if (this->IsSneaking())

		eyeHeight -= 0.08f;

	return Vector3{

		pos.x,

		pos.y + eyeHeight,

		pos.z

	};

}



Vector3 CEntity::GetLastTickPos()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_lastTickPosX || !StrayCache::entity_lastTickPosY || !StrayCache::entity_lastTickPosZ)

		return Vector3{};



	return Vector3{

		(float)(double)env->GetDoubleField(inst, StrayCache::entity_lastTickPosX),

		(float)(double)env->GetDoubleField(inst, StrayCache::entity_lastTickPosY),

		(float)(double)env->GetDoubleField(inst, StrayCache::entity_lastTickPosZ)

	};

}



bool CEntity::IsSneaking()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_isSneaking)

		return false;

	bool v = env->CallBooleanMethod(inst, StrayCache::entity_isSneaking);

	JniResolve::ClearException(env);

	return v;

}



float CEntity::GetHeight()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_height)

		return 0.0f;

	float v = env->GetFloatField(inst, StrayCache::entity_height);

	JniResolve::ClearException(env);

	return v;

}



float CEntity::GetWidth()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_width)

		return 0.0f;

	float v = env->GetFloatField(inst, StrayCache::entity_width);

	JniResolve::ClearException(env);

	return v;

}



float CEntity::GetDistanceWalkedModified()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_distanceWalkedModified)

		return 0.0f;

	float v = env->GetFloatField(inst, StrayCache::entity_distanceWalkedModified);

	JniResolve::ClearException(env);

	return v;

}



float CEntity::GetPrevDistanceWalkedModified()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_prevDistanceWalkedModified)

		return 0.0f;

	float v = env->GetFloatField(inst, StrayCache::entity_prevDistanceWalkedModified);

	JniResolve::ClearException(env);

	return v;

}



float CEntity::GetRotationYaw()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_rotationYaw)

		return 0.0f;

	float v = env->GetFloatField(inst, StrayCache::entity_rotationYaw);

	JniResolve::ClearException(env);

	return v;

}



float CEntity::GetRotationPitch()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_rotationPitch)

		return 0.0f;

	float v = env->GetFloatField(inst, StrayCache::entity_rotationPitch);

	JniResolve::ClearException(env);

	return v;

}



float CEntity::GetPrevRotationYaw()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_prevRotationYaw)

		return 0.0f;

	float v = env->GetFloatField(inst, StrayCache::entity_prevRotationYaw);

	JniResolve::ClearException(env);

	return v;

}



float CEntity::GetPrevRotationPitch()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_prevRotationPitch)

		return 0.0f;

	float v = env->GetFloatField(inst, StrayCache::entity_prevRotationPitch);

	JniResolve::ClearException(env);

	return v;

}



Vector2 CEntity::GetAngles()

{

	return Vector2(CEntity::GetRotationYaw(), CEntity::GetRotationPitch());

}



Vector2 CEntity::GetPrevAngles()

{

	return Vector2(CEntity::GetPrevRotationYaw(), CEntity::GetPrevRotationPitch());

}



void CEntity::SetAngles(Vector2 angles) 

{

	if (!StrayCache::entity_rotationYaw || !StrayCache::entity_rotationPitch)

		return;

	jobject inst = this->GetInstance();

	JNIEnv* env = Java::GetEnv();

	if (!env || !inst)

		return;

	env->SetFloatField(inst, StrayCache::entity_rotationYaw, angles.x);
	env->SetFloatField(inst, StrayCache::entity_rotationPitch, angles.y);

	JniResolve::ClearException(env);

};



void CEntity::SetAnglesWithPrev(Vector2 angles)

{

	if (!StrayCache::entity_rotationYaw || !StrayCache::entity_rotationPitch)

		return;

	jobject inst = this->GetInstance();

	JNIEnv* env = Java::GetEnv();

	if (!env || !inst)

		return;

	env->SetFloatField(inst, StrayCache::entity_rotationYaw, angles.x);
	env->SetFloatField(inst, StrayCache::entity_rotationPitch, angles.y);
	if (StrayCache::entity_prevRotationYaw)
		env->SetFloatField(inst, StrayCache::entity_prevRotationYaw, angles.x);
	if (StrayCache::entity_prevRotationPitch)
		env->SetFloatField(inst, StrayCache::entity_prevRotationPitch, angles.y);

	JniResolve::ClearException(env);

};



CAxisAlignedBB CEntity::GetBB()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_boundingBox)

		return CAxisAlignedBB((jobject)nullptr);

	return CAxisAlignedBB(env->GetObjectField(inst, StrayCache::entity_boundingBox));

}



void CEntity::SetBB(BoundingBox bb)

{

	this->GetBB().SetBoundingBox(bb);

}



bool CEntity::IsSprinting()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_isSprinting)

		return false;

	bool v = env->CallBooleanMethod(inst, StrayCache::entity_isSprinting);

	JniResolve::ClearException(env);

	return v;

}

void CEntity::SetSprinting(bool sprinting)

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_setSprinting)

		return;

	env->CallVoidMethod(inst, StrayCache::entity_setSprinting, sprinting ? JNI_TRUE : JNI_FALSE);

	JniResolve::ClearException(env);

}

bool CEntity::IsMovingForward()

{

	if ((GetAsyncKeyState('W') & 0x8000) != 0)

		return true;

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_motionX || !StrayCache::entity_motionZ)

		return false;

	double mx = env->GetDoubleField(inst, StrayCache::entity_motionX);

	double mz = env->GetDoubleField(inst, StrayCache::entity_motionZ);

	JniResolve::ClearException(env);

	return (mx * mx + mz * mz) > 0.001;

}



bool CEntity::IsInWater()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_isInWater)

		return false;

	bool v = env->CallBooleanMethod(inst, StrayCache::entity_isInWater);

	JniResolve::ClearException(env);

	return v;

}



bool CEntity::GetOnGround()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_onGround)

		return false;

	bool v = env->GetBooleanField(inst, StrayCache::entity_onGround);

	JniResolve::ClearException(env);

	return v;

}



float CEntity::GetFallDistance()

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_fallDistance)

		return 0.f;

	float v = env->GetFloatField(inst, StrayCache::entity_fallDistance);

	JniResolve::ClearException(env);

	return v;

}



void CEntity::SetPos(Vector3 pos)

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_posX || !StrayCache::entity_posY || !StrayCache::entity_posZ)

		return;

	env->SetDoubleField(inst, StrayCache::entity_posX, (double)pos.x);

	env->SetDoubleField(inst, StrayCache::entity_posY, (double)pos.y);

	env->SetDoubleField(inst, StrayCache::entity_posZ, (double)pos.z);

	JniResolve::ClearException(env);

}



void CEntity::SetLastTickPos(Vector3 pos)

{

	JNIEnv* env = Java::GetEnv();

	jobject inst = GetInstance();

	if (!env || !inst || !StrayCache::entity_lastTickPosX || !StrayCache::entity_lastTickPosY || !StrayCache::entity_lastTickPosZ)

		return;

	env->SetDoubleField(inst, StrayCache::entity_lastTickPosX, (double)pos.x);

	env->SetDoubleField(inst, StrayCache::entity_lastTickPosY, (double)pos.y);

	env->SetDoubleField(inst, StrayCache::entity_lastTickPosZ, (double)pos.z);

	JniResolve::ClearException(env);

}

