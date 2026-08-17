#include "InventoryPlayer.h"

#include "../../../../strayCache.h"

CInventoryPlayer::CInventoryPlayer()
{
	if (!StrayCache::initialized) StrayCache::Initialize();
	this->Class = StrayCache::inventoryPlayer_class;
}

CInventoryPlayer::CInventoryPlayer(jobject instance) : CInventoryPlayer()
{
	this->Instance = instance;
}

jclass CInventoryPlayer::GetClass()
{
	return this->Class;
}

jobject CInventoryPlayer::GetInstance()
{
	return this->Instance;
}

CItemStack CInventoryPlayer::GetCurrentItem()
{
	// CallObjectMethod with a null instance or method ID crashes the JVM.
	jobject instance = this->GetInstance();
	if (!instance || !StrayCache::inventoryPlayer_getCurrentItem)
		return CItemStack(nullptr);
	return CItemStack(Java::Env->CallObjectMethod(instance, StrayCache::inventoryPlayer_getCurrentItem));
}
