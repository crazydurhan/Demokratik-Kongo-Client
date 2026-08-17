#include "EntityLivingBase.h"

#include "../../../../java/java.h"
#include "../../../strayCache.h"
#include "../../../jniResolve.h"

CEntityLivingBase::CEntityLivingBase()
{
	if (!StrayCache::initialized) StrayCache::Initialize();
	this->Class = StrayCache::entityLivingBase_class;
}

CEntityLivingBase::CEntityLivingBase(jobject instance) : CEntityLivingBase()
{
	this->Instance = instance;
}

jclass CEntityLivingBase::GetClass()
{
	return this->Class;
}

jobject CEntityLivingBase::GetInstance()
{
	return this->Instance;
}

float CEntityLivingBase::GetHealth()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	if (!env || !inst || !StrayCache::entityLivingBase_getHealth)
		return 0.0f;
	float v = env->CallFloatMethod(inst, StrayCache::entityLivingBase_getHealth);
	JniResolve::ClearException(env);
	return v;
}

float CEntityLivingBase::GetMaxHealth()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	if (!env || !inst || !StrayCache::entityLivingBase_getMaxHealth)
		return 0.0f;
	float v = env->CallFloatMethod(inst, StrayCache::entityLivingBase_getMaxHealth);
	JniResolve::ClearException(env);
	return v;
}

float CEntityLivingBase::GetAbsorptionAmount()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	if (!env || !inst)
		return 0.0f;
	// Resolved once: this ran GetObjectClass + up to two GetMethodID calls for
	// every player on every data tick.
	static jmethodID s_mid = nullptr;
	static bool s_resolveFailed = false;

	if (!s_mid && !s_resolveFailed)
	{
		jclass cls = env->GetObjectClass(inst);
		if (cls)
		{
			s_mid = env->GetMethodID(cls, "func_110139_bj", "()F");
			JniResolve::ClearException(env);
			if (!s_mid)
			{
				s_mid = env->GetMethodID(cls, "getAbsorptionAmount", "()F");
				JniResolve::ClearException(env);
			}
			env->DeleteLocalRef(cls);
		}
		if (!s_mid)
			s_resolveFailed = true;
	}

	if (!s_mid)
		return 0.0f;

	const float v = env->CallFloatMethod(inst, s_mid);
	JniResolve::ClearException(env);
	return v;
}

bool CEntityLivingBase::CanEntityBeSeen(jobject entity) 
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	if (!env || !inst || !entity || !StrayCache::entityLivingBase_canEntityBeSeen)
		return false;
	bool v = env->CallBooleanMethod(inst, StrayCache::entityLivingBase_canEntityBeSeen, entity);
	JniResolve::ClearException(env);
	return v;
}

void CEntityLivingBase::SwingItem()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	if (!env || !inst || !StrayCache::entityLivingBase_swingItem)
		return;
	env->CallVoidMethod(inst, StrayCache::entityLivingBase_swingItem);
	JniResolve::ClearException(env);
}

int CEntityLivingBase::GetJumpTicks()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	if (!env || !inst || !StrayCache::entityLivingBase_jumpTicks)
		return 0;
	int v = env->GetIntField(inst, StrayCache::entityLivingBase_jumpTicks);
	JniResolve::ClearException(env);
	return v;
}

void CEntityLivingBase::SetJumpTicks(int value)
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	if (!env || !inst || !StrayCache::entityLivingBase_jumpTicks)
		return;
	env->SetIntField(inst, StrayCache::entityLivingBase_jumpTicks, value);
	JniResolve::ClearException(env);
}

bool CEntityLivingBase::IsPotionActive(int potionId)
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	if (!env || !inst)
		return false;

	jmethodID mid = StrayCache::entityLivingBase_isPotionActive;
	if (!mid && StrayCache::entityLivingBase_class)
	{
		mid = JniResolve::Method(env, StrayCache::entityLivingBase_class, "(I)Z", "isPotionActive");
		if (!mid) mid = JniResolve::Method(env, StrayCache::entityLivingBase_class, "(I)Z", "func_70644_a");
		if (!mid) mid = JniResolve::Method(env, StrayCache::entityLivingBase_class, "(I)Z", "func_82165_m");
		if (mid) StrayCache::entityLivingBase_isPotionActive = mid;
	}
	if (!mid) return false;

	bool v = env->CallBooleanMethod(inst, mid, (jint)potionId);
	JniResolve::ClearException(env);
	return v;
}

void CEntityLivingBase::RemovePotionEffect(int potionId)
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	if (!env || !inst)
		return;

	jmethodID mid = StrayCache::entityLivingBase_removePotionEffect;
	if (!mid && StrayCache::entityLivingBase_class)
	{
		mid = JniResolve::Method(env, StrayCache::entityLivingBase_class, "(I)V", "removePotionEffect");
		if (!mid) mid = JniResolve::Method(env, StrayCache::entityLivingBase_class, "(I)V", "func_70618_k");
		if (!mid) mid = JniResolve::Method(env, StrayCache::entityLivingBase_class, "(I)V", "func_82170_o");
		if (!mid) mid = JniResolve::Method(env, StrayCache::entityLivingBase_class, "(I)V", "removePotionEffectClient");
		if (mid) StrayCache::entityLivingBase_removePotionEffect = mid;
	}
	if (!mid) return;

	env->CallVoidMethod(inst, mid, (jint)potionId);
	JniResolve::ClearException(env);
}