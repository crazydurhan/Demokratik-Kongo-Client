#include "EntityPlayer.h"

#include "../../../../../java/java.h"
#include "../../../../jniResolve.h"

CEntityPlayer::CEntityPlayer()
{
	Java::AssignClass("net.minecraft.entity.player.EntityPlayer", this->Class);
	// JniResolve clears the pending NoSuchFieldError on obfuscated clients;
	// a raw GetFieldID here poisoned the env for every SDK ctor that followed.
	this->FieldIDs["inventory"] = JniResolve::Field(Java::Env, this->Class,
		"Lnet/minecraft/entity/player/InventoryPlayer;", "inventory");
}

CEntityPlayer::CEntityPlayer(jobject instance) : CEntityPlayer()
{
	this->Instance = instance;
}


jclass CEntityPlayer::GetClass()
{
	return this->Class;
}

jobject CEntityPlayer::GetInstance()
{
	return this->Instance;
}

CInventoryPlayer CEntityPlayer::GetInventory()
{
	// GetObjectField with a null field ID or instance is undefined behavior
	// (hard JVM crash), not a null return — guard both.
	jfieldID inventoryField = this->FieldIDs["inventory"];
	jobject instance = this->GetInstance();
	if (!inventoryField || !instance)
		return CInventoryPlayer(nullptr);
	return CInventoryPlayer(Java::Env->GetObjectField(instance, inventoryField));
}
