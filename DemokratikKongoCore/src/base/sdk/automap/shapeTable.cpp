#include "shapeTable.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	// Distinctive 1.8.9 MCP shapes for the StrayCache / SDK set.
	// Counts are lower bounds so mixin extra fields still match.
	const ShapeRef kRefs[] = {
		{ "net/minecraft/client/Minecraft", nullptr, 0, 0, 4, 2, 20, 1, true, 40 },
		{ "net/minecraft/entity/Entity", "DDDDDDDDDFFFF", 9, 8, 1, 6, 3, 1, false, 30 },
		{ "net/minecraft/entity/EntityLivingBase", nullptr, 0, 2, 4, 0, 2, 2, false, 10 },
		{ "net/minecraft/entity/player/EntityPlayer", nullptr, 0, 0, 1, 2, 4, 3, false, 8 },
		{ "net/minecraft/client/entity/EntityPlayerSP", nullptr, 0, 2, 0, 0, 2, 5, false, 6 },
		{ "net/minecraft/util/Timer", nullptr, 1, 3, 1, 0, 0, 1, false, 5 },
		{ "net/minecraft/util/Vec3", "DDD", 3, 0, 0, 0, 0, 1, false, 3 },
		{ "net/minecraft/util/AxisAlignedBB", "DDDDDD", 6, 0, 0, 0, 0, 1, false, 6 },
		{ "net/minecraft/client/settings/GameSettings", nullptr, 2, 4, 8, 8, 8, 1, false, 40 },
		{ "net/minecraft/client/multiplayer/PlayerControllerMP", nullptr, 0, 1, 2, 1, 2, 1, false, 8 },
		{ "net/minecraft/client/renderer/entity/RenderManager", nullptr, 3, 2, 0, 0, 2, 1, false, 8 },
		{ "net/minecraft/entity/player/InventoryPlayer", nullptr, 0, 0, 1, 0, 2, 1, false, 6 },
		{ "net/minecraft/item/ItemStack", nullptr, 0, 0, 2, 0, 1, 1, false, 4 },
		{ "net/minecraft/world/World", nullptr, 0, 0, 2, 2, 6, 1, false, 20 },
		{ "net/minecraft/client/multiplayer/WorldClient", nullptr, 0, 0, 0, 0, 4, 2, false, 8 },
		{ "net/minecraft/util/MovingObjectPosition", nullptr, 0, 0, 0, 0, 3, 1, false, 4 },
	};
}

const ShapeRef* ShapeTable::Refs() { return kRefs; }
unsigned int ShapeTable::RefCount() { return sizeof(kRefs) / sizeof(kRefs[0]); }

const ShapeRef* ShapeTable::FindRef(const char* mcp)
{
	if (!mcp)
		return nullptr;
	for (unsigned int i = 0; i < RefCount(); ++i)
	{
		if (std::strcmp(kRefs[i].mcp, mcp) == 0)
			return &kRefs[i];
	}
	return nullptr;
}

float ShapeTable::HistCosine(const ClassShape& a, const ClassShape& b)
{
	const float av[7] = {
		(float)a.d, (float)a.f, (float)a.i, (float)a.z, (float)a.j, (float)a.L, (float)a.arr
	};
	const float bv[7] = {
		(float)b.d, (float)b.f, (float)b.i, (float)b.z, (float)b.j, (float)b.L, (float)b.arr
	};
	float dot = 0.f, na = 0.f, nb = 0.f;
	for (int i = 0; i < 7; ++i)
	{
		dot += av[i] * bv[i];
		na += av[i] * av[i];
		nb += bv[i] * bv[i];
	}
	if (na <= 0.f || nb <= 0.f)
		return 0.f;
	return dot / (std::sqrt(na) * std::sqrt(nb));
}

float ShapeTable::MethodJaccard(const ClassShape& a, const ClassShape& b)
{
	if (a.methodPrims.empty() && b.methodPrims.empty())
		return 1.f;
	int inter = 0;
	for (const auto& m : a.methodPrims)
	{
		if (std::find(b.methodPrims.begin(), b.methodPrims.end(), m) != b.methodPrims.end())
			++inter;
	}
	const int uni = (int)a.methodPrims.size() + (int)b.methodPrims.size() - inter;
	return uni <= 0 ? 0.f : (float)inter / (float)uni;
}

int ShapeTable::LcsLength(const std::string& a, const std::string& b)
{
	const size_t n = a.size(), m = b.size();
	if (!n || !m)
		return 0;
	std::vector<int> prev(m + 1, 0), cur(m + 1, 0);
	for (size_t i = 1; i <= n; ++i)
	{
		for (size_t j = 1; j <= m; ++j)
		{
			if (a[i - 1] == b[j - 1])
				cur[j] = prev[j - 1] + 1;
			else
				cur[j] = (prev[j] > cur[j - 1]) ? prev[j] : cur[j - 1];
		}
		prev.swap(cur);
		std::fill(cur.begin(), cur.end(), 0);
	}
	return prev[m];
}

bool ShapeTable::ContainsSeq(const std::string& hay, const char* needle)
{
	return needle && *needle && hay.find(needle) != std::string::npos;
}

float ShapeTable::Score(const ShapeRef& ref, const ClassShape& live)
{
	float s = 0.f;
	if (live.d >= ref.minD) s += 0.15f;
	if (live.f >= ref.minF) s += 0.10f;
	if (live.i >= ref.minI) s += 0.08f;
	if (live.z >= ref.minZ) s += 0.08f;
	if (live.L >= ref.minL) s += 0.12f;
	if (live.fieldCount >= ref.minFields) s += 0.10f;
	if (live.superDepth == ref.superDepth) s += 0.15f;
	else if (std::abs(live.superDepth - ref.superDepth) == 1) s += 0.05f;
	if (ref.staticSelf)
		s += live.staticSelf ? 0.15f : -0.20f;
	if (ref.distinctiveSeq && ContainsSeq(live.primSeq, ref.distinctiveSeq))
		s += 0.20f;
	return s;
}
