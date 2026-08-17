#pragma once

#include "../../../java/IClass.h"
#include "../entity/player/EntityPlayer.h"
#include "../../../../util/math/geometry.h"

#include <vector>

struct RayResult {
	bool hit;
	Vector3 pos;
};

struct CWorld : IClass
{
	CWorld();

	jclass GetClass();
	jobject GetInstance();

	std::vector<CEntityPlayer> GetPlayerList();

	RayResult rayTraceBlocks(Vector3 from, Vector3 to, bool stopOnLiquid = false,
	                         bool ignoreBlockWithoutBoundingBox = true,
	                         bool returnLastUncollidableBlock = false);

private:
	jclass m_vec3Class = nullptr;
	jmethodID m_vec3Init = nullptr;
};

