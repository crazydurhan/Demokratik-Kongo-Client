#include "sdk.h"
#include "../util/logger.h"
#include "../java/java.h"
#include "strayCache.h"

void SDK::Init()
{
	StrayCache::Initialize();
	if (!StrayCache::IsReady())
		Logger::Err("[SDK] StrayCache not ready - continuing with partial mappings.");
	if (!SDK::Minecraft)
		SDK::Minecraft = new CMinecraft();
	if (SDK::Minecraft && !SDK::Minecraft->IsReady())
		Logger::Err("[SDK] CMinecraft mappings incomplete.");
}
