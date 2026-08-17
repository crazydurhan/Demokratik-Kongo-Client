#include "World.h"

#include "../../../sdk.h"
#include "../../../../java/java.h"
#include "../../../java/util/List.h"
#include "../util/Vec3.h"
#include "../util/MovingObjectPosition.h"
#include "../../../strayCache.h"
#include "../../../jniResolve.h"

CWorld::CWorld() 
{
	Java::AssignClass("net.minecraft.world.World", this->Class);
	this->FieldIDs["playerEntities"] = StrayCache::world_playerEntities;

	this->MethodIDs["rayTraceBlocks"] = JniResolve::Method(Java::GetEnv(), this->Class,
		"(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;ZZZ)Lnet/minecraft/util/MovingObjectPosition;",
		"rayTraceBlocks");

	Java::AssignClass("net.minecraft.util.Vec3", m_vec3Class);
	if (m_vec3Class)
		m_vec3Init = Java::Env->GetMethodID(m_vec3Class, "<init>", "(DDD)V");
}

jclass CWorld::GetClass()
{
	return this->Class;
}

jobject CWorld::GetInstance()
{
	if (!SDK::Minecraft) return nullptr;
	jobject mcInstance = SDK::Minecraft->GetInstance();
	if (!mcInstance) return nullptr;
	return Java::Env->GetObjectField(mcInstance, SDK::Minecraft->FieldIDs["theWorld"]);
}

	std::vector<CEntityPlayer> CWorld::GetPlayerList()
{
	std::vector<CEntityPlayer> finalList;
	jfieldID targetField = this->FieldIDs["playerEntities"];
	if (!targetField) return finalList;

	jobject worldInstance = this->GetInstance();
	if (!worldInstance) return finalList;

	jobject playerEntitiesList = Java::Env->GetObjectField(worldInstance, this->FieldIDs["playerEntities"]);
	Java::Env->DeleteLocalRef(worldInstance);

	jobjectArray playerEntities = List::List(playerEntitiesList).toArray();
	if (!playerEntities)
	{
		if (playerEntitiesList) Java::Env->DeleteLocalRef(playerEntitiesList);
		return finalList;
	}
	int size = Java::Env->GetArrayLength(playerEntities);
	
	for (int i = 0; i < size; i++)
	{
		jobject obj_player = Java::Env->GetObjectArrayElement(playerEntities, i);
		if (!obj_player) continue;

		CEntityPlayer player =  CEntityPlayer::CEntityPlayer(obj_player);
		finalList.push_back(player);
		Java::Env->DeleteLocalRef(obj_player);
	}

	Java::Env->DeleteLocalRef(playerEntitiesList);
	Java::Env->DeleteLocalRef(playerEntities);

	return finalList;
}

RayResult CWorld::rayTraceBlocks(Vector3 from, Vector3 to, bool stopOnLiquid, bool ignoreBlockWithoutBoundingBox, bool returnLastUncollidableBlock)
{
	if (!m_vec3Class || !m_vec3Init || !this->MethodIDs["rayTraceBlocks"]) {
		return { false, Vector3{} };
	}

	jobject j_from = Java::Env->NewObject(m_vec3Class, m_vec3Init, (jdouble)from.x, (jdouble)from.y, (jdouble)from.z);
	jobject j_to   = Java::Env->NewObject(m_vec3Class, m_vec3Init, (jdouble)to.x, (jdouble)to.y, (jdouble)to.z);

	jobject movingObjPos_j = Java::Env->CallObjectMethod(
		this->GetInstance(),
		this->MethodIDs["rayTraceBlocks"],
		j_from,
		j_to,
		stopOnLiquid,
		ignoreBlockWithoutBoundingBox,
		returnLastUncollidableBlock
	);

	if (!movingObjPos_j) {
		Java::Env->DeleteLocalRef(j_to);
		Java::Env->DeleteLocalRef(j_from);
		return { false, Vector3{} };
	}

	CMovingObjectPosition movingObjPos = CMovingObjectPosition(movingObjPos_j);
	Java::Env->DeleteLocalRef(movingObjPos_j);
	CVec3 a = movingObjPos.GetBlockPosition();
	Vector3 blockPos = a.GetNativeVector3();
	Java::Env->DeleteLocalRef(j_to);
	Java::Env->DeleteLocalRef(j_from);
	return { true, blockPos };
}
