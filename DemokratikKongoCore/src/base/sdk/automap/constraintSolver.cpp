#include "constraintSolver.h"
#include "liveInspector.h"
#include "shapeTable.h"
#include "classIndex.h"
#include "automapUtil.h"

#include "../../java/java.h"
#include "../jniResolve.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace
{
	void clearEx(JNIEnv* env)
	{
		if (env && env->ExceptionCheck())
			env->ExceptionClear();
	}

	bool alreadyClass(const std::vector<SolvedClass>& classes, const char* mcp)
	{
		for (const auto& c : classes)
			if (c.mcp == mcp)
				return true;
		return false;
	}

	jclass findBound(const std::vector<SolvedClass>& classes, const char* mcp)
	{
		for (const auto& c : classes)
			if (c.mcp == mcp)
				return c.cls;
		return nullptr;
	}

	void addClass(std::vector<SolvedClass>& classes, const char* mcp, jclass cls, ResolveVia via, float conf, JNIEnv* env)
	{
		if (!mcp || !cls || alreadyClass(classes, mcp))
			return;
		SolvedClass sc;
		sc.mcp = mcp;
		sc.cls = static_cast<jclass>(env->NewGlobalRef(cls));
		sc.via = via;
		sc.confidence = conf;
		sc.runtimeName = LiveInspector::ClassSlashName(env, cls);
		classes.push_back(std::move(sc));
	}

	void addField(std::vector<SolvedMember>& members, const char* owner, const char* name,
		jfieldID id, ResolveVia via, float conf, const std::string& runtime, const char* sig)
	{
		if (!id)
			return;
		for (const auto& m : members)
			if (m.owner == owner && m.mcpName == name)
				return;
		SolvedMember sm;
		sm.owner = owner;
		sm.mcpName = name;
		sm.isField = true;
		sm.fid = id;
		sm.via = via;
		sm.confidence = conf;
		sm.runtimeName = runtime;
		sm.sig = sig ? sig : "";
		members.push_back(std::move(sm));
	}

	void addMethod(std::vector<SolvedMember>& members, const char* owner, const char* name,
		jmethodID id, bool isStatic, ResolveVia via, float conf, const std::string& runtime, const char* sig)
	{
		if (!id)
			return;
		for (const auto& m : members)
			if (m.owner == owner && m.mcpName == name)
				return;
		SolvedMember sm;
		sm.owner = owner;
		sm.mcpName = name;
		sm.isField = false;
		sm.isStatic = isStatic;
		sm.mid = id;
		sm.via = via;
		sm.confidence = conf;
		sm.runtimeName = runtime;
		sm.sig = sig ? sig : "";
		members.push_back(std::move(sm));
	}

	jmethodID findStaticSelfGetter(JNIEnv* env, jclass cls)
	{
		jclass classClass = env->FindClass("java/lang/Class");
		if (!classClass) { clearEx(env); return nullptr; }
		jmethodID getMethods = env->GetMethodID(classClass, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
		env->DeleteLocalRef(classClass);
		if (!getMethods) { clearEx(env); return nullptr; }

		jclass methodClass = env->FindClass("java/lang/reflect/Method");
		if (!methodClass) { clearEx(env); return nullptr; }
		jmethodID getName = env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;");
		jmethodID getRet = env->GetMethodID(methodClass, "getReturnType", "()Ljava/lang/Class;");
		jmethodID getParams = env->GetMethodID(methodClass, "getParameterTypes", "()[Ljava/lang/Class;");
		jmethodID getMods = env->GetMethodID(methodClass, "getModifiers", "()I");
		jclass modClass = env->FindClass("java/lang/reflect/Modifier");
		jmethodID isStatic = modClass ? env->GetStaticMethodID(modClass, "isStatic", "(I)Z") : nullptr;
		if (!getName || !getRet || !getParams || !getMods || !isStatic)
		{
			clearEx(env);
			env->DeleteLocalRef(methodClass);
			if (modClass) env->DeleteLocalRef(modClass);
			return nullptr;
		}

		jobjectArray methods = (jobjectArray)env->CallObjectMethod(cls, getMethods);
		clearEx(env);
		jmethodID result = nullptr;
		std::string resultName;
		if (methods)
		{
			const jsize n = env->GetArrayLength(methods);
			for (jsize i = 0; i < n && !result; ++i)
			{
				jobject m = env->GetObjectArrayElement(methods, i);
				if (!m) continue;
				jint mods = env->CallIntMethod(m, getMods);
				if (env->CallStaticBooleanMethod(modClass, isStatic, mods) != JNI_TRUE)
				{
					env->DeleteLocalRef(m);
					continue;
				}
				jobjectArray params = (jobjectArray)env->CallObjectMethod(m, getParams);
				clearEx(env);
				const jsize pc = params ? env->GetArrayLength(params) : -1;
				if (params) env->DeleteLocalRef(params);
				if (pc != 0)
				{
					env->DeleteLocalRef(m);
					continue;
				}
				jclass rt = (jclass)env->CallObjectMethod(m, getRet);
				clearEx(env);
				if (rt && env->IsSameObject(rt, cls))
					result = env->FromReflectedMethod(m);
				if (rt) env->DeleteLocalRef(rt);
				env->DeleteLocalRef(m);
			}
			env->DeleteLocalRef(methods);
		}
		env->DeleteLocalRef(methodClass);
		if (modClass) env->DeleteLocalRef(modClass);
		return result;
	}

	const char* kEntityChain[] = {
		"net/minecraft/client/entity/EntityPlayerSP",
		"net/minecraft/client/entity/AbstractClientPlayer",
		"net/minecraft/entity/player/EntityPlayer",
		"net/minecraft/entity/EntityLivingBase",
		"net/minecraft/entity/Entity",
	};
}

void ConstraintSolver::Run(JNIEnv* env, jclass minecraft, std::vector<SolvedClass>& classes, std::vector<SolvedMember>& members)
{
	if (!env || !minecraft)
		return;

	JniResolve::LocalFrame frame(env, 128);
	addClass(classes, "net/minecraft/client/Minecraft", minecraft, ResolveVia::Constraint, 1.f, env);

	const ClassShape mcShape = LiveInspector::Measure(env, minecraft);
	if (mcShape.staticSelf)
	{
		jmethodID getMc = findStaticSelfGetter(env, minecraft);
		if (getMc)
		{
			addMethod(members, "net/minecraft/client/Minecraft", "getMinecraft",
				getMc, true, ResolveVia::StaticSelf, 1.f,
				LiveInspector::MethodRuntimeName(env, minecraft, getMc, "()L;"),
				"()Lnet/minecraft/client/Minecraft;");
		}
	}

	auto fields = LiveInspector::ListFields(env, minecraft, true);

	struct Cand { InspectedField f; const ShapeRef* ref; float score; };
	std::vector<Cand> cands;
	for (const auto& f : fields)
	{
		if (f.isStatic || f.prim != 'L' || !f.typeClass)
			continue;
		const ClassShape sh = LiveInspector::Measure(env, f.typeClass);
		const ShapeRef* bestRef = nullptr;
		float best = 0.f;
		float second = 0.f;
		for (unsigned int i = 0; i < ShapeTable::RefCount(); ++i)
		{
			const ShapeRef& ref = ShapeTable::Refs()[i];
			if (std::strcmp(ref.mcp, "net/minecraft/client/Minecraft") == 0)
				continue;
			const float sc = ShapeTable::Score(ref, sh);
			if (sc > best)
			{
				second = best;
				best = sc;
				bestRef = &ref;
			}
			else if (sc > second)
				second = sc;
		}
		if (bestRef && best >= 0.55f && (best - second) >= 0.12f)
			cands.push_back({ f, bestRef, best });
	}

	std::unordered_map<std::string, int> refHits;
	for (const auto& c : cands)
		++refHits[c.ref->mcp];

	for (const auto& c : cands)
	{
		if (refHits[c.ref->mcp] != 1)
			continue;
		addClass(classes, c.ref->mcp, c.f.typeClass, ResolveVia::UniqueType, c.score, env);
		const char* fieldName =
			(std::strcmp(c.ref->mcp, "net/minecraft/util/Timer") == 0) ? "timer" :
			(std::strcmp(c.ref->mcp, "net/minecraft/client/settings/GameSettings") == 0) ? "gameSettings" :
			(std::strcmp(c.ref->mcp, "net/minecraft/client/multiplayer/PlayerControllerMP") == 0) ? "playerController" :
			(std::strcmp(c.ref->mcp, "net/minecraft/client/renderer/entity/RenderManager") == 0) ? "renderManager" :
			(std::strcmp(c.ref->mcp, "net/minecraft/util/MovingObjectPosition") == 0) ? "objectMouseOver" :
			(std::strcmp(c.ref->mcp, "net/minecraft/client/entity/EntityPlayerSP") == 0) ? "thePlayer" :
			(std::strcmp(c.ref->mcp, "net/minecraft/client/multiplayer/WorldClient") == 0) ? "theWorld" :
			nullptr;
		if (fieldName)
			addField(members, "net/minecraft/client/Minecraft", fieldName,
				c.f.id, ResolveVia::UniqueType, c.score, c.f.name, nullptr);
	}

	// thePlayer: deepest Entity-like field type
	InspectedField playerField;
	jclass playerCls = nullptr;
	int bestDepth = 0;
	for (const auto& f : fields)
	{
		if (f.isStatic || f.prim != 'L' || !f.typeClass)
			continue;
		const ClassShape sh = LiveInspector::Measure(env, f.typeClass);
		if (sh.d >= 9 && sh.superDepth >= 4 && sh.superDepth > bestDepth)
		{
			bestDepth = sh.superDepth;
			playerField = f;
			playerCls = f.typeClass;
		}
	}
	if (playerCls)
	{
		addClass(classes, "net/minecraft/client/entity/EntityPlayerSP", playerCls, ResolveVia::SuperChain, 0.95f, env);
		addField(members, "net/minecraft/client/Minecraft", "thePlayer",
			playerField.id, ResolveVia::SuperChain, 0.95f, playerField.name,
			"Lnet/minecraft/client/entity/EntityPlayerSP;");

		jclass cur = playerCls;
		for (int i = 0; i < 5 && cur; ++i)
		{
			addClass(classes, kEntityChain[i], cur, ResolveVia::SuperChain, 0.9f, env);
			jclass parent = env->GetSuperclass(cur);
			clearEx(env);
			if (cur != playerCls && cur != minecraft)
				env->DeleteLocalRef(cur);
			cur = parent;
		}
		if (cur && cur != playerCls)
			env->DeleteLocalRef(cur);
	}

	jclass entity = findBound(classes, "net/minecraft/entity/Entity");
	if (entity)
	{
		auto efields = LiveInspector::ListFields(env, entity, true);
		std::vector<InspectedField> ds, fs;
		for (const auto& f : efields)
		{
			if (f.isStatic) continue;
			if (f.prim == 'D') ds.push_back(f);
			if (f.prim == 'F') fs.push_back(f);
		}
		if (ds.size() >= 7)
		{
			const size_t posBase = (ds.size() >= 10) ? 4 : 3;
			addField(members, "net/minecraft/entity/Entity", "posX", ds[posBase].id, ResolveVia::PrimSeq, 0.9f, ds[posBase].name, "D");
			addField(members, "net/minecraft/entity/Entity", "posY", ds[posBase + 1].id, ResolveVia::PrimSeq, 0.9f, ds[posBase + 1].name, "D");
			addField(members, "net/minecraft/entity/Entity", "posZ", ds[posBase + 2].id, ResolveVia::PrimSeq, 0.9f, ds[posBase + 2].name, "D");
		}
		if (ds.size() >= 10)
		{
			addField(members, "net/minecraft/entity/Entity", "motionX", ds[7].id, ResolveVia::PrimSeq, 0.85f, ds[7].name, "D");
			addField(members, "net/minecraft/entity/Entity", "motionY", ds[8].id, ResolveVia::PrimSeq, 0.85f, ds[8].name, "D");
			addField(members, "net/minecraft/entity/Entity", "motionZ", ds[9].id, ResolveVia::PrimSeq, 0.85f, ds[9].name, "D");
		}
		if (ds.size() >= 12)
		{
			addField(members, "net/minecraft/entity/Entity", "lastTickPosX", ds[ds.size() - 3].id, ResolveVia::PrimSeq, 0.8f, ds[ds.size() - 3].name, "D");
			addField(members, "net/minecraft/entity/Entity", "lastTickPosY", ds[ds.size() - 2].id, ResolveVia::PrimSeq, 0.8f, ds[ds.size() - 2].name, "D");
			addField(members, "net/minecraft/entity/Entity", "lastTickPosZ", ds[ds.size() - 1].id, ResolveVia::PrimSeq, 0.8f, ds[ds.size() - 1].name, "D");
		}
		if (fs.size() >= 2)
		{
			addField(members, "net/minecraft/entity/Entity", "rotationYaw", fs[0].id, ResolveVia::PrimSeq, 0.85f, fs[0].name, "F");
			addField(members, "net/minecraft/entity/Entity", "rotationPitch", fs[1].id, ResolveVia::PrimSeq, 0.85f, fs[1].name, "F");
		}
		if (fs.size() >= 4)
		{
			addField(members, "net/minecraft/entity/Entity", "prevRotationYaw", fs[2].id, ResolveVia::PrimSeq, 0.8f, fs[2].name, "F");
			addField(members, "net/minecraft/entity/Entity", "prevRotationPitch", fs[3].id, ResolveVia::PrimSeq, 0.8f, fs[3].name, "F");
		}
	}

	// Score remaining unbound refs against loaded classes (filtered).
	std::unordered_set<std::string> boundRuntime;
	for (const auto& c : classes)
		if (!c.runtimeName.empty())
			boundRuntime.insert(c.runtimeName);

	for (unsigned int i = 0; i < ShapeTable::RefCount(); ++i)
	{
		const ShapeRef& ref = ShapeTable::Refs()[i];
		if (alreadyClass(classes, ref.mcp))
			continue;

		jclass bestCls = nullptr;
		std::string bestPath;
		float best = 0.f, second = 0.f;
		ClassIndex::ForEach([&](const std::string& path, jclass cls) {
			if (!cls || AutoMapUtil::IsJdkPath(path) || boundRuntime.count(path))
				return;
			if (path.find('$') != std::string::npos)
				return;
			const ClassShape sh = LiveInspector::Measure(env, cls);
			const float sc = ShapeTable::Score(ref, sh);
			if (sc > best)
			{
				second = best;
				best = sc;
				bestCls = cls;
				bestPath = path;
			}
			else if (sc > second)
				second = sc;
		});
		if (bestCls && best >= 0.70f && (best - second) >= 0.15f)
		{
			addClass(classes, ref.mcp, bestCls, ResolveVia::Score, best, env);
			boundRuntime.insert(bestPath);
		}
	}
}
