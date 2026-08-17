#include "WorldClient.h"

#include "../../../../sdk.h"
#include "../../../../../java/java.h"
#include "../../../../../util/logger.h"
#include "../../../../jniResolve.h"
#include "../../../../java/util/Set.h"

CWorldClient::CWorldClient()
{
	Java::AssignClass("net.minecraft.client.multiplayer.WorldClient", this->Class);
	Java::AssignClass("net.minecraft.entity.player.EntityPlayer", this->EntityPlayer); // doing this because am lazy

	// JniResolve clears the pending NoSuchFieldError on obfuscated clients;
	// a raw GetFieldID here poisoned the env for every SDK ctor that followed.
	this->FieldIDs["entityList"] = JniResolve::Field(Java::Env, this->Class, "Ljava/util/Set;", "entityList");
}

jclass CWorldClient::GetClass()
{
	return this->Class;
}

jobject CWorldClient::GetInstance()
{
	if (!SDK::Minecraft) return nullptr;
	jobject mcInstance = SDK::Minecraft->GetInstance();
	if (!mcInstance) return nullptr;
	return Java::Env->GetObjectField(mcInstance, SDK::Minecraft->FieldIDs["theWorld"]);
}

std::vector<CEntity> CWorldClient::GetEntityList()
{
	std::vector<CEntity> finalList;
	jfieldID targetField = this->FieldIDs["entityList"];
	jobject instance = this->GetInstance();
	if (!targetField || !instance)
		return finalList;

	jobject playerEntitiesList = Java::Env->GetObjectField(instance, targetField);
	if (!playerEntitiesList) return finalList;

	jobjectArray playerEntities = Set::Set(playerEntitiesList).toArray();
	if (playerEntities) {
		int size = Java::Env->GetArrayLength(playerEntities);

		for (int i = 0; i < size; i++)
		{
			jobject obj_player = Java::Env->GetObjectArrayElement(playerEntities, i);
			if (!obj_player) continue;

			CEntity player = CEntity::CEntity(obj_player);
			if (player.GetName().compare(SDK::Minecraft->thePlayer->GetName()))
				finalList.push_back(player);
			Java::Env->DeleteLocalRef(obj_player);
		}
		Java::Env->DeleteLocalRef(playerEntities);
	}
	Java::Env->DeleteLocalRef(playerEntitiesList);
	return finalList;
}