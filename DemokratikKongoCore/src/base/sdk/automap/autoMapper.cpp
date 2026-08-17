#include "autoMapper.h"
#include "classIndex.h"
#include "nameScheme.h"
#include "signatureRemap.h"
#include "autoMapReport.h"
#include "constraintSolver.h"
#include "liveInspector.h"
#include "automapUtil.h"

#include "../gen/vapeMappingData.h"
#include "../mappingLoader.h"
#include "../mappedRegistry.h"
#include "../jniResolve.h"
#include "../../java/java.h"
#include "../../util/logger.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	std::mutex g_mutex;
	bool g_ready = false;

	std::unordered_map<std::string, jclass> g_classes;
	std::unordered_map<std::string, jfieldID> g_fields;
	std::unordered_map<std::string, jmethodID> g_methods;
	std::unordered_map<std::string, std::string> g_runtimeNames;

	struct Needed
	{
		ReportKind kind;
		const char* owner;
		const char* name; // null for class-only
		const char* sig;
		bool isStatic;
		bool critical;
	};

	const Needed kNeeded[] = {
		{ ReportKind::Class, "net/minecraft/client/Minecraft", nullptr, nullptr, false, true },
		{ ReportKind::Class, "net/minecraft/entity/Entity", nullptr, nullptr, false, true },
		{ ReportKind::Class, "net/minecraft/entity/EntityLivingBase", nullptr, nullptr, false, true },
		{ ReportKind::Class, "net/minecraft/entity/player/EntityPlayer", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/client/entity/EntityPlayerSP", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/client/multiplayer/PlayerControllerMP", nullptr, nullptr, false, true },
		{ ReportKind::Class, "net/minecraft/client/multiplayer/WorldClient", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/world/World", nullptr, nullptr, false, true },
		{ ReportKind::Class, "net/minecraft/util/AxisAlignedBB", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/util/MovingObjectPosition", nullptr, nullptr, false, true },
		{ ReportKind::Class, "net/minecraft/util/Vec3", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/util/Timer", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/client/settings/GameSettings", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/client/renderer/entity/RenderManager", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/entity/player/InventoryPlayer", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/item/ItemStack", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/item/Item", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/item/ItemBlock", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/util/IChatComponent", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/enchantment/EnchantmentHelper", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/client/gui/inventory/GuiInventory", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/client/renderer/EntityRenderer", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/client/renderer/entity/RendererLivingEntity", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/client/renderer/entity/RenderPlayer", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/client/gui/GuiIngame", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/client/gui/GuiScreen", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/network/NetworkManager", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/util/BlockPos", nullptr, nullptr, false, false },
		{ ReportKind::Class, "net/minecraft/util/EnumFacing", nullptr, nullptr, false, false },

		{ ReportKind::Field, "net/minecraft/entity/Entity", "posX", "D", false, true },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "posY", "D", false, true },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "posZ", "D", false, true },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "lastTickPosX", "D", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "lastTickPosY", "D", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "lastTickPosZ", "D", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "motionX", "D", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "motionY", "D", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "motionZ", "D", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "rotationYaw", "F", false, true },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "rotationPitch", "F", false, true },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "prevRotationYaw", "F", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "prevRotationPitch", "F", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "width", "F", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "height", "F", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "onGround", "Z", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "fallDistance", "F", false, false },
		{ ReportKind::Field, "net/minecraft/entity/Entity", "boundingBox", "Lnet/minecraft/util/AxisAlignedBB;", false, false },
		{ ReportKind::Method, "net/minecraft/entity/Entity", "getName", "()Ljava/lang/String;", false, false },
		{ ReportKind::Method, "net/minecraft/entity/Entity", "isSneaking", "()Z", false, false },
		{ ReportKind::Method, "net/minecraft/entity/Entity", "isInvisible", "()Z", false, false },
		{ ReportKind::Method, "net/minecraft/entity/Entity", "setSprinting", "(Z)V", false, false },
		{ ReportKind::Method, "net/minecraft/entity/Entity", "isSprinting", "()Z", false, false },
		{ ReportKind::Method, "net/minecraft/entity/Entity", "isInWater", "()Z", false, false },

		{ ReportKind::Field, "net/minecraft/entity/EntityLivingBase", "hurtTime", "I", false, true },
		{ ReportKind::Method, "net/minecraft/entity/EntityLivingBase", "getHeldItem", "()Lnet/minecraft/item/ItemStack;", false, true },
		{ ReportKind::Method, "net/minecraft/entity/EntityLivingBase", "getHealth", "()F", false, false },
		{ ReportKind::Method, "net/minecraft/entity/EntityLivingBase", "swingItem", "()V", false, false },

		{ ReportKind::Field, "net/minecraft/client/Minecraft", "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;", false, true },
		{ ReportKind::Field, "net/minecraft/client/Minecraft", "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;", false, true },
		{ ReportKind::Field, "net/minecraft/client/Minecraft", "objectMouseOver", "Lnet/minecraft/util/MovingObjectPosition;", false, true },
		{ ReportKind::Field, "net/minecraft/client/Minecraft", "leftClickCounter", "I", false, true },
		{ ReportKind::Field, "net/minecraft/client/Minecraft", "rightClickDelayTimer", "I", false, false },
		{ ReportKind::Field, "net/minecraft/client/Minecraft", "inGameHasFocus", "Z", false, false },
		{ ReportKind::Field, "net/minecraft/client/Minecraft", "playerController", "Lnet/minecraft/client/multiplayer/PlayerControllerMP;", false, false },
		{ ReportKind::Field, "net/minecraft/client/Minecraft", "gameSettings", "Lnet/minecraft/client/settings/GameSettings;", false, false },
		{ ReportKind::Field, "net/minecraft/client/Minecraft", "timer", "Lnet/minecraft/util/Timer;", false, false },
		{ ReportKind::Field, "net/minecraft/client/Minecraft", "renderManager", "Lnet/minecraft/client/renderer/entity/RenderManager;", false, false },
		{ ReportKind::Method, "net/minecraft/client/Minecraft", "getMinecraft", "()Lnet/minecraft/client/Minecraft;", true, true },
		{ ReportKind::Method, "net/minecraft/client/Minecraft", "clickMouse", "()V", false, true },
		{ ReportKind::Method, "net/minecraft/client/Minecraft", "rightClickMouse", "()V", false, false },

		{ ReportKind::Method, "net/minecraft/client/multiplayer/PlayerControllerMP", "attackEntity",
			"(Lnet/minecraft/entity/player/EntityPlayer;Lnet/minecraft/entity/Entity;)V", false, true },
		{ ReportKind::Method, "net/minecraft/client/multiplayer/PlayerControllerMP", "windowClick",
			"(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;", false, false },
		{ ReportKind::Method, "net/minecraft/client/multiplayer/PlayerControllerMP", "syncCurrentPlayItem", "()V", false, false },

		{ ReportKind::Field, "net/minecraft/world/World", "playerEntities", "Ljava/util/List;", false, true },
	};

	std::string memberKey(const char* owner, const char* name)
	{
		std::string k = owner ? owner : "";
		k += '#';
		k += name ? name : "";
		return k;
	}

	jclass lookupClassUnlocked(const char* owner)
	{
		if (!owner)
			return nullptr;
		auto it = g_classes.find(owner);
		return it == g_classes.end() ? nullptr : it->second;
	}

	void bindClass(const char* mcp, jclass cls, const char* runtime, ResolveVia via, float conf, bool critical)
	{
		if (!mcp || !cls)
			return;
		if (g_classes.find(mcp) == g_classes.end())
			g_classes.emplace(mcp, cls);
		if (runtime && *runtime)
		{
			g_runtimeNames[mcp] = runtime;
			SignatureRemap::Bind(mcp, runtime);
		}
		AutoMapEntry e;
		e.kind = ReportKind::Class;
		e.owner = mcp;
		e.runtimeName = runtime ? runtime : "";
		e.via = AutoMapReport::ViaName(via);
		e.confidence = conf;
		e.found = true;
		e.critical = critical;
		AutoMapReport::Add(e);
	}

	void missClass(const char* mcp, const char* reason, bool critical)
	{
		AutoMapEntry e;
		e.kind = ReportKind::Class;
		e.owner = mcp ? mcp : "";
		e.failReason = reason ? reason : "no-class";
		e.found = false;
		e.critical = critical;
		AutoMapReport::Add(e);
	}

	jclass resolveClassFast(const char* mcp)
	{
		if (!mcp)
			return nullptr;
		if (jclass hit = ClassIndex::Find(mcp))
			return hit;
		if (const char* notch = NameSchemeDetect::NotchAlias(mcp))
			return ClassIndex::Find(notch);
		return nullptr;
	}

	void resolveAllClassesFast()
	{
		JNIEnv* env = Java::GetEnv();
		for (unsigned int i = 0; i < VapeMapData::kClassAliasCount; ++i)
		{
			const char* mcp = VapeMapData::kClassAliases[i].mcp;
			const char* notch = VapeMapData::kClassAliases[i].notch;
			jclass cls = ClassIndex::Find(mcp);
			const char* runtime = mcp;
			ResolveVia via = ResolveVia::ClassIndex;
			if (!cls && notch)
			{
				cls = ClassIndex::Find(notch);
				runtime = notch;
			}
			if (!cls)
				continue;
			g_classes[mcp] = cls;
			g_runtimeNames[mcp] = runtime;
			SignatureRemap::Bind(mcp, runtime);
			(void)env;
			(void)via;
		}

		for (const Needed& n : kNeeded)
		{
			if (n.kind != ReportKind::Class)
				continue;
			jclass cls = lookupClassUnlocked(n.owner);
			std::string runtime;
			ResolveVia via = ResolveVia::ClassIndex;
			if (!cls)
			{
				cls = resolveClassFast(n.owner);
				if (cls)
				{
					runtime = LiveInspector::ClassSlashName(Java::GetEnv(), cls);
					bindClass(n.owner, cls, runtime.c_str(), via, 1.f, n.critical);
				}
				else
					missClass(n.owner, "no-class", n.critical);
			}
			else
			{
				runtime = g_runtimeNames.count(n.owner) ? g_runtimeNames[n.owner] : n.owner;
				bindClass(n.owner, cls, runtime.c_str(), via, 1.f, n.critical);
			}
		}
	}

	void resolveMembersFast()
	{
		JNIEnv* env = Java::GetEnv();
		if (!env)
			return;

		for (const Needed& n : kNeeded)
		{
			if (n.kind == ReportKind::Class)
				continue;

			jclass cls = lookupClassUnlocked(n.owner);
			AutoMapEntry e;
			e.kind = n.kind;
			e.owner = n.owner;
			e.mcpName = n.name ? n.name : "";
			e.sig = n.sig ? n.sig : "";
			e.critical = n.critical;

			if (!cls)
			{
				e.found = false;
				e.failReason = "no-class";
				AutoMapReport::Add(e);
				continue;
			}

			const std::string remapped = SignatureRemap::Apply(n.sig);
			const char* useSig = remapped.empty() ? n.sig : remapped.c_str();
			e.sig = useSig ? useSig : "";

			if (n.kind == ReportKind::Field)
			{
				jfieldID id = n.isStatic
					? nullptr
					: JniResolve::Field(env, cls, useSig, n.name);
				if (!id && n.isStatic)
				{
					for (const std::string& name : MappingLoader::Names(n.name))
					{
						id = env->GetStaticFieldID(cls, name.c_str(), useSig);
						if (env->ExceptionCheck()) { env->ExceptionClear(); id = nullptr; }
						if (id) break;
					}
				}
				if (!id)
					id = JniResolve::FieldByDescriptor(env, cls, useSig, n.isStatic);

				if (id)
				{
					g_fields[memberKey(n.owner, n.name)] = id;
					e.found = true;
					e.via = AutoMapReport::ViaName(ResolveVia::Table);
					e.confidence = 1.f;
					e.runtimeName = LiveInspector::FieldRuntimeName(env, cls, id);
				}
				else
				{
					e.found = false;
					e.failReason = "no-unique-desc";
				}
			}
			else
			{
				jmethodID id = n.isStatic
					? JniResolve::StaticMethod(env, cls, useSig, n.name)
					: JniResolve::Method(env, cls, useSig, n.name);
				if (!id)
					id = JniResolve::MethodByDescriptor(env, cls, useSig);

				if (id)
				{
					g_methods[memberKey(n.owner, n.name)] = id;
					e.found = true;
					e.via = AutoMapReport::ViaName(ResolveVia::Descriptor);
					e.confidence = 0.95f;
					e.runtimeName = LiveInspector::MethodRuntimeName(env, cls, id, useSig);
				}
				else
				{
					e.found = false;
					e.failReason = "no-unique-desc";
				}
			}
			AutoMapReport::Add(e);
		}
	}

	bool hasCriticalMiss()
	{
		for (const Needed& n : kNeeded)
		{
			if (!n.critical)
				continue;
			if (n.kind == ReportKind::Class && !lookupClassUnlocked(n.owner))
				return true;
			if (n.kind == ReportKind::Field && g_fields.find(memberKey(n.owner, n.name)) == g_fields.end())
				return true;
			if (n.kind == ReportKind::Method && g_methods.find(memberKey(n.owner, n.name)) == g_methods.end())
				return true;
		}
		return false;
	}

	void applySolved(const std::vector<SolvedClass>& classes, const std::vector<SolvedMember>& members)
	{
		for (const auto& c : classes)
		{
			if (!c.cls)
				continue;
			if (g_classes.find(c.mcp) == g_classes.end())
			{
				g_classes[c.mcp] = c.cls;
				if (!c.runtimeName.empty())
				{
					g_runtimeNames[c.mcp] = c.runtimeName;
					SignatureRemap::Bind(c.mcp.c_str(), c.runtimeName.c_str());
				}
			}
		}
		for (const auto& m : members)
		{
			const std::string key = memberKey(m.owner.c_str(), m.mcpName.c_str());
			if (m.isField && m.fid && g_fields.find(key) == g_fields.end())
				g_fields[key] = m.fid;
			if (!m.isField && m.mid && g_methods.find(key) == g_methods.end())
				g_methods[key] = m.mid;
		}
	}

	void rebuildReportAfterSolve(const std::vector<SolvedClass>& classes, const std::vector<SolvedMember>& members)
	{
		// Overlay solver hits onto missing report rows by appending OK lines.
		for (const auto& c : classes)
		{
			if (!c.cls)
				continue;
			AutoMapEntry e;
			e.kind = ReportKind::Class;
			e.owner = c.mcp;
			e.runtimeName = c.runtimeName;
			e.via = AutoMapReport::ViaName(c.via);
			e.confidence = c.confidence;
			e.found = true;
			AutoMapReport::Add(e);
		}
		for (const auto& m : members)
		{
			AutoMapEntry e;
			e.kind = m.isField ? ReportKind::Field : ReportKind::Method;
			e.owner = m.owner;
			e.mcpName = m.mcpName;
			e.runtimeName = m.runtimeName;
			e.sig = m.sig;
			e.via = AutoMapReport::ViaName(m.via);
			e.confidence = m.confidence;
			e.found = (m.isField && m.fid) || (!m.isField && m.mid);
			AutoMapReport::Add(e);
		}
	}
}

void AutoMapper::Init()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_ready)
		return;

	g_classes.clear();
	g_fields.clear();
	g_methods.clear();
	g_runtimeNames.clear();
	SignatureRemap::Clear();
	AutoMapReport::Reset();

	MappingLoader::Init();
	MappedRegistry::Init();
	NameSchemeDetect::Init();

	resolveAllClassesFast();
	resolveMembersFast();

	if (hasCriticalMiss())
	{
		JNIEnv* env = Java::GetEnv();
		jclass mc = lookupClassUnlocked("net/minecraft/client/Minecraft");
		if (!mc)
			mc = ClassIndex::Find("ave");
		if (!mc && env)
			mc = LiveInspector::FindMinecraftFromClientThread(env);

		if (mc)
		{
			std::vector<SolvedClass> solvedC;
			std::vector<SolvedMember> solvedM;
			ConstraintSolver::Run(env, mc, solvedC, solvedM);
			applySolved(solvedC, solvedM);
			rebuildReportAfterSolve(solvedC, solvedM);
			Logger::Log("[AutoMapper] constraint solver ran (" +
				std::to_string(solvedC.size()) + " classes, " +
				std::to_string(solvedM.size()) + " members)");
		}
		else
			Logger::Warn("AutoMap", "Minecraft class not found — solver skipped");
	}

	g_ready = true;
	Logger::Log("[AutoMapper] ready — " + std::to_string(g_classes.size()) + " classes, "
		+ std::to_string(g_fields.size()) + " fields, "
		+ std::to_string(g_methods.size()) + " methods");
}

void AutoMapper::Shutdown()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_classes.clear();
	g_fields.clear();
	g_methods.clear();
	g_runtimeNames.clear();
	g_ready = false;
}

bool AutoMapper::IsReady()
{
	return g_ready;
}

jclass AutoMapper::Class(const char* ownerPath)
{
	if (!ownerPath)
		return nullptr;
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_classes.find(ownerPath);
	if (it != g_classes.end())
		return it->second;
	const std::string slash = AutoMapUtil::ToSlash(ownerPath);
	it = g_classes.find(slash);
	return it == g_classes.end() ? nullptr : it->second;
}

jfieldID AutoMapper::Field(const char* ownerPath, const char* mcpName, const char* sig)
{
	(void)sig;
	if (!ownerPath || !mcpName)
		return nullptr;
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_fields.find(memberKey(ownerPath, mcpName));
	return it == g_fields.end() ? nullptr : it->second;
}

jmethodID AutoMapper::Method(const char* ownerPath, const char* mcpName, const char* sig)
{
	(void)sig;
	if (!ownerPath || !mcpName)
		return nullptr;
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_methods.find(memberKey(ownerPath, mcpName));
	return it == g_methods.end() ? nullptr : it->second;
}

const char* AutoMapper::RuntimeName(const char* ownerPath)
{
	if (!ownerPath)
		return nullptr;
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_runtimeNames.find(ownerPath);
	if (it != g_runtimeNames.end())
		return it->second.c_str();
	const std::string slash = AutoMapUtil::ToSlash(ownerPath);
	it = g_runtimeNames.find(slash);
	return it == g_runtimeNames.end() ? nullptr : it->second.c_str();
}
