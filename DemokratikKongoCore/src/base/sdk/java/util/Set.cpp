#include "Set.h"
#include "../../../java/java.h"
#include "../../strayCache.h"

Set::Set(jobject jset)
{
	this->Instance = jset;
	if (!StrayCache::initialized) StrayCache::Initialize();
	this->Class = StrayCache::set_class;
	this->MethodIDs["toArray"] = StrayCache::set_toArray;
}

jclass Set::GetClass()
{
	return this->Class;
}

jobject Set::GetInstance()
{
	return this->Instance;
}

jobjectArray Set::toArray()
{
	return (jobjectArray)Java::Env->CallObjectMethod(this->Instance, this->MethodIDs["toArray"]);
}
