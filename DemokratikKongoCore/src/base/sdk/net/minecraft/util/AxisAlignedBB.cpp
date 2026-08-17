#include "AxisAlignedBB.h"

#include "../../../../java/java.h"
#include "../../../../util/logger.h"
#include "../../../strayCache.h"
#include "../../../jniResolve.h"

CAxisAlignedBB::CAxisAlignedBB()
{
	if (!StrayCache::initialized) StrayCache::Initialize();
	this->Class = StrayCache::axisAlignedBB_class;
}

CAxisAlignedBB::CAxisAlignedBB(jobject instance) : CAxisAlignedBB()
{
	this->Instance = instance;
}

jclass CAxisAlignedBB::GetClass()
{
	return this->Class;
}

jobject CAxisAlignedBB::GetInstance()
{
	return this->Instance;
}

BoundingBox CAxisAlignedBB::GetNativeBoundingBox()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	if (!env || !inst
		|| !StrayCache::axisAlignedBB_minX || !StrayCache::axisAlignedBB_minY || !StrayCache::axisAlignedBB_minZ
		|| !StrayCache::axisAlignedBB_maxX || !StrayCache::axisAlignedBB_maxY || !StrayCache::axisAlignedBB_maxZ)
		return BoundingBox{};

	return BoundingBox{
		env->GetDoubleField(inst, StrayCache::axisAlignedBB_minX),
		env->GetDoubleField(inst, StrayCache::axisAlignedBB_minY),
		env->GetDoubleField(inst, StrayCache::axisAlignedBB_minZ),
		env->GetDoubleField(inst, StrayCache::axisAlignedBB_maxX),
		env->GetDoubleField(inst, StrayCache::axisAlignedBB_maxY),
		env->GetDoubleField(inst, StrayCache::axisAlignedBB_maxZ),
	};
}

void CAxisAlignedBB::SetBoundingBox(BoundingBox newBoundingBox)
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	if (!env || !inst
		|| !StrayCache::axisAlignedBB_minX || !StrayCache::axisAlignedBB_minY || !StrayCache::axisAlignedBB_minZ
		|| !StrayCache::axisAlignedBB_maxX || !StrayCache::axisAlignedBB_maxY || !StrayCache::axisAlignedBB_maxZ)
		return;

	env->SetDoubleField(inst, StrayCache::axisAlignedBB_minX, newBoundingBox.minX);
	env->SetDoubleField(inst, StrayCache::axisAlignedBB_minY, newBoundingBox.minY);
	env->SetDoubleField(inst, StrayCache::axisAlignedBB_minZ, newBoundingBox.minZ);
	env->SetDoubleField(inst, StrayCache::axisAlignedBB_maxX, newBoundingBox.maxX);
	env->SetDoubleField(inst, StrayCache::axisAlignedBB_maxY, newBoundingBox.maxY);
	env->SetDoubleField(inst, StrayCache::axisAlignedBB_maxZ, newBoundingBox.maxZ);
	JniResolve::ClearException(env);
}
