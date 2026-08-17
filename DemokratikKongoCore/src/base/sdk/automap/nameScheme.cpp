#include "nameScheme.h"
#include "classIndex.h"

#include "../gen/vapeMappingData.h"
#include "../../java/java.h"
#include "../../util/logger.h"

#include <cstring>
#include <string>
#include <unordered_map>

namespace
{
	NameScheme g_scheme = NameScheme::Unknown;
	std::unordered_map<std::string, std::string> g_mcpToNotch;
	std::unordered_map<std::string, std::string> g_notchToMcp;

	void ensureAliasMaps()
	{
		if (!g_mcpToNotch.empty())
			return;
		for (unsigned int i = 0; i < VapeMapData::kClassAliasCount; ++i)
		{
			g_mcpToNotch.emplace(VapeMapData::kClassAliases[i].mcp, VapeMapData::kClassAliases[i].notch);
			g_notchToMcp.emplace(VapeMapData::kClassAliases[i].notch, VapeMapData::kClassAliases[i].mcp);
		}
	}

	bool classPresent(const char* slash)
	{
		return ClassIndex::Find(slash) != nullptr;
	}

	bool memberResolves(jclass cls, const char* name, const char* sig, bool isField)
	{
		JNIEnv* env = Java::GetEnv();
		if (!env || !cls || !name || !sig)
			return false;
		if (isField)
		{
			jfieldID id = env->GetFieldID(cls, name, sig);
			if (env->ExceptionCheck()) { env->ExceptionClear(); id = nullptr; }
			if (id) return true;
			id = env->GetStaticFieldID(cls, name, sig);
			if (env->ExceptionCheck()) { env->ExceptionClear(); id = nullptr; }
			return id != nullptr;
		}
		jmethodID id = env->GetMethodID(cls, name, sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); id = nullptr; }
		if (id) return true;
		id = env->GetStaticMethodID(cls, name, sig);
		if (env->ExceptionCheck()) { env->ExceptionClear(); id = nullptr; }
		return id != nullptr;
	}
}

void NameSchemeDetect::Init()
{
	ensureAliasMaps();

	const bool hasMcpMc = classPresent("net/minecraft/client/Minecraft");
	const bool hasNotchMc = classPresent("ave");

	jclass mc = ClassIndex::Find("net/minecraft/client/Minecraft");
	if (!mc)
		mc = ClassIndex::Find("ave");

	bool mcpMember = false;
	bool srgMember = false;
	bool notchMember = false;
	if (mc)
	{
		// thePlayer: MCP / SRG / Notch. Primitive-free object sig is remapped later;
		// probe with a remapped-free unique primitive first (inGameHasFocus Z).
		mcpMember = memberResolves(mc, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;", true)
			|| memberResolves(mc, "inGameHasFocus", "Z", true)
			|| memberResolves(mc, "getMinecraft", "()Lnet/minecraft/client/Minecraft;", false);
		srgMember = memberResolves(mc, "field_71439_g", "Lnet/minecraft/client/entity/EntityPlayerSP;", true)
			|| memberResolves(mc, "field_71415_G", "Z", true)
			|| memberResolves(mc, "func_71410_x", "()Lnet/minecraft/client/Minecraft;", false);
		notchMember = memberResolves(mc, "h", "Lbew;", true)
			|| memberResolves(mc, "w", "Z", true)
			|| memberResolves(mc, "A", "()Lave;", false);
	}

	if (hasNotchMc && !hasMcpMc)
		g_scheme = NameScheme::Notch;
	else if (hasMcpMc && srgMember && !mcpMember)
		g_scheme = NameScheme::SrgMembers;
	else if (hasMcpMc && mcpMember && srgMember)
		g_scheme = NameScheme::Mixed;
	else if (hasMcpMc && mcpMember)
		g_scheme = NameScheme::McpNamed;
	else if (hasNotchMc && notchMember)
		g_scheme = NameScheme::Notch;
	else
		g_scheme = NameScheme::Unknown;

	Logger::Log(std::string("[NameScheme] ") + SchemeName(g_scheme)
		+ " mcpMc=" + (hasMcpMc ? "1" : "0")
		+ " notchMc=" + (hasNotchMc ? "1" : "0"));
}

NameScheme NameSchemeDetect::Scheme()
{
	return g_scheme;
}

const char* NameSchemeDetect::SchemeName(NameScheme scheme)
{
	switch (scheme)
	{
	case NameScheme::McpNamed:   return "McpNamed";
	case NameScheme::SrgMembers: return "SrgMembers";
	case NameScheme::Notch:      return "Notch";
	case NameScheme::Mixed:      return "Mixed";
	default:                     return "Unknown";
	}
}

const char* NameSchemeDetect::NotchAlias(const char* mcpSlash)
{
	if (!mcpSlash)
		return nullptr;
	ensureAliasMaps();
	auto it = g_mcpToNotch.find(mcpSlash);
	return it == g_mcpToNotch.end() ? nullptr : it->second.c_str();
}

const char* NameSchemeDetect::McpFromNotch(const char* notch)
{
	if (!notch)
		return nullptr;
	ensureAliasMaps();
	auto it = g_notchToMcp.find(notch);
	return it == g_notchToMcp.end() ? nullptr : it->second.c_str();
}
