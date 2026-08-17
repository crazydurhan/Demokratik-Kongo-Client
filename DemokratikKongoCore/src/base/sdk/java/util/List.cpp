#include "List.h"
#include "../../../java/java.h"
#include "../../strayCache.h"

List::List(jobject jlist)
{
	this->Instance = jlist;
	if (!StrayCache::initialized) StrayCache::Initialize();
	this->Class = StrayCache::list_class;
	this->MethodIDs["toArray"] = StrayCache::list_toArray;
}

jclass List::GetClass()
{
	return this->Class;
}

jobject List::GetInstance()
{
	return this->Instance;
}

jobjectArray List::toArray()
{
	return (jobjectArray) Java::Env->CallObjectMethod(this->GetInstance(), this->MethodIDs["toArray"]);
}
