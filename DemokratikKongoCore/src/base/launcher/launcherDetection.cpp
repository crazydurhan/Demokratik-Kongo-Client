#include "launcherDetection.h"

#include "../java/java.h"
#include "../sdk/automap/classIndex.h"
#include "../util/logger.h"

namespace
{
	LauncherKind g_kind = LauncherKind::Unknown;
	bool g_initialized = false;

	bool classExists(const char* slashName)
	{
		if (ClassIndex::Find(slashName))
			return true;
		if (!Java::Initialized || !Java::Env || !Java::tiEnv)
			return false;
		jclass cls = Java::findClass(Java::Env, Java::tiEnv, slashName);
		if (cls)
		{
			Java::Env->DeleteLocalRef(cls);
			return true;
		}
		return false;
	}
}

void LauncherDetection::Init()
{
	if (g_initialized)
		return;
	g_initialized = true;

	if (!Java::Initialized)
	{
		g_kind = LauncherKind::Unknown;
		Logger::Warn("Launcher", "Java not initialized — launcher kind unknown");
		return;
	}

	if (classExists("com/moonsworth/lunar/genesis/Genesis") ||
		classExists("lunar/GenesisLauncher"))
	{
		g_kind = LauncherKind::Lunar;
	}
	else if (classExists("net/badlion/client/Wrapper") ||
		classExists("net/badlion/client/Client") ||
		classExists("net/badlion/client/tweaker/BadlionTransformer"))
	{
		g_kind = LauncherKind::Badlion;
	}
	else if (classExists("net/digitalingot/feather/Feather") ||
		classExists("net/digitalingot/feather/FeatherMod") ||
		classExists("com/featherclient/Feather"))
	{
		g_kind = LauncherKind::Feather;
	}
	else if (classExists("net/minecraftforge/fml/common/Loader") ||
		classExists("net/minecraftforge/fml/loading/FMLLoader") ||
		classExists("cpw/mods/fml/common/Loader") ||
		classExists("net/minecraft/launchwrapper/Launch"))
	{
		g_kind = LauncherKind::Forge;
	}
	else if (classExists("optifine/OptiFineTweaker") ||
		classExists("net/optifine/Config"))
	{
		g_kind = LauncherKind::OptiFine;
	}
	else if (classExists("net/minecraft/client/Minecraft") ||
		classExists("net/minecraft/client/main/Main") ||
		classExists("ave"))
	{
		g_kind = LauncherKind::Vanilla;
	}
	else
	{
		g_kind = LauncherKind::Unknown;
	}

	Logger::Info("Launcher", std::string("Detected launcher: ") + KindName(g_kind));
}

LauncherKind LauncherDetection::Kind()
{
	return g_kind;
}

const char* LauncherDetection::KindName(LauncherKind kind)
{
	switch (kind)
	{
	case LauncherKind::Vanilla:  return "Vanilla";
	case LauncherKind::Forge:    return "Forge";
	case LauncherKind::Lunar:    return "Lunar";
	case LauncherKind::Badlion:  return "Badlion";
	case LauncherKind::Feather:  return "Feather";
	case LauncherKind::OptiFine: return "OptiFine";
	default:                     return "Unknown";
	}
}
