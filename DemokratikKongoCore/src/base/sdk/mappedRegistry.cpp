#include "mappedRegistry.h"

#include "gen/vapeMappingData.h"
#include "mappingLoader.h"
#include "jniResolve.h"
#include "automap/autoMapper.h"
#include "automap/classIndex.h"
#include "automap/nameScheme.h"
#include "automap/signatureRemap.h"

#include "../java/java.h"
#include "../util/logger.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	std::mutex g_mutex;
	bool g_ready = false;
	bool g_notchEnv = false; // true when running against vanilla-obfuscated classes

	std::unordered_map<std::string, std::vector<const VapeMapData::Entry*>> g_byOwner;
	std::unordered_map<std::string, std::string> g_notchClass; // mcp path -> notch path

	std::unordered_map<std::string, jclass> g_classCache;
	std::unordered_set<std::string> g_failedClasses;

	std::unordered_map<const VapeMapData::Entry*, void*> g_memberCache;
	std::unordered_set<const VapeMapData::Entry*> g_failedMembers;

	std::string ToDotName(const char* internal)
	{
		std::string out = internal ? internal : "";
		for (auto& c : out)
			if (c == '/') c = '.';
		return out;
	}

	std::vector<std::string> SplitNames(const char* names)
	{
		std::vector<std::string> out;
		std::string cur;
		for (const char* p = names; p && *p; ++p)
		{
			if (*p == '|') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
			else cur.push_back(*p);
		}
		if (!cur.empty()) out.push_back(cur);
		return out;
	}

	// Rewrites Lmcp/class; segments of a JNI signature to their Notch form.
	// Only used when a vanilla (obfuscated) runtime was detected.
	std::string RemapSignature(const char* sig)
	{
		if (!sig) return "";
		std::string out;
		out.reserve(strlen(sig) + 8);
		for (const char* p = sig; *p; ++p)
		{
			if (*p == 'L')
			{
				const char* end = strchr(p, ';');
				if (end)
				{
					std::string cls(p + 1, end - p - 1);
					auto it = g_notchClass.find(cls);
					if (it != g_notchClass.end())
					{
						out += 'L';
						out += it->second;
						out += ';';
						p = end;
						continue;
					}
				}
			}
			out += *p;
		}
		return out;
	}

	void ClearPending(JNIEnv* env)
	{
		if (env && env->ExceptionCheck())
			env->ExceptionClear();
	}

	jclass ResolveClass(JNIEnv* env, const std::string& owner)
	{
		auto it = g_classCache.find(owner);
		if (it != g_classCache.end())
			return it->second;
		if (g_failedClasses.count(owner))
			return nullptr;

		jclass cls = nullptr;
		if (AutoMapper::IsReady())
			cls = AutoMapper::Class(owner.c_str());
		if (!cls)
			cls = ClassIndex::Find(owner.c_str());
		if (!cls)
		{
			if (const char* notch = NameSchemeDetect::NotchAlias(owner.c_str()))
				cls = ClassIndex::Find(notch);
		}
		if (!cls && !Java::AssignClass(ToDotName(owner.c_str()), cls))
		{
			auto alias = g_notchClass.find(owner);
			if (alias != g_notchClass.end())
				Java::AssignClass(ToDotName(alias->second.c_str()), cls);
		}
		if (cls && g_classCache.find(owner) == g_classCache.end())
			cls = static_cast<jclass>(env->NewGlobalRef(cls));

		if (cls)
		{
			g_classCache.emplace(owner, cls);
			return cls;
		}
		g_failedClasses.insert(owner);
		return nullptr;
	}

	// --- reflection fallback for entries without a resolvable signature ---

	jfieldID FieldByNameReflection(JNIEnv* env, jclass cls, const std::vector<std::string>& names)
	{
		jclass classClass = env->FindClass("java/lang/Class");
		if (!classClass) { ClearPending(env); return nullptr; }
		jmethodID getDeclaredFields = env->GetMethodID(classClass, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");
		env->DeleteLocalRef(classClass);
		if (!getDeclaredFields) { ClearPending(env); return nullptr; }

		jclass fieldClass = env->FindClass("java/lang/reflect/Field");
		if (!fieldClass) { ClearPending(env); return nullptr; }
		jmethodID getName = env->GetMethodID(fieldClass, "getName", "()Ljava/lang/String;");
		if (!getName) { ClearPending(env); env->DeleteLocalRef(fieldClass); return nullptr; }

		jfieldID result = nullptr;
		jclass current = cls;
		while (current && !result)
		{
			jobjectArray fields = (jobjectArray)env->CallObjectMethod(current, getDeclaredFields);
			if (env->ExceptionCheck()) { env->ExceptionClear(); fields = nullptr; }
			if (fields)
			{
				const jsize count = env->GetArrayLength(fields);
				for (jsize i = 0; i < count && !result; ++i)
				{
					jobject f = env->GetObjectArrayElement(fields, i);
					if (!f) { ClearPending(env); continue; }
					jstring jname = (jstring)env->CallObjectMethod(f, getName);
					if (jname)
					{
						const char* utf = env->GetStringUTFChars(jname, nullptr);
						bool match = false;
						if (utf)
						{
							for (const auto& n : names)
								if (n == utf) { match = true; break; }
						}
						if (match)
							result = env->FromReflectedField(f);
						if (utf) env->ReleaseStringUTFChars(jname, utf);
						env->DeleteLocalRef(jname);
					}
					ClearPending(env);
					env->DeleteLocalRef(f);
				}
				env->DeleteLocalRef(fields);
			}
			if (!result)
			{
				jclass parent = env->GetSuperclass(current);
				ClearPending(env);
				if (current != cls) env->DeleteLocalRef(current);
				current = parent;
			}
		}
		if (current && current != cls) env->DeleteLocalRef(current);
		env->DeleteLocalRef(fieldClass);
		return result;
	}

	jmethodID MethodByNameReflection(JNIEnv* env, jclass cls, const std::vector<std::string>& names)
	{
		jclass classClass = env->FindClass("java/lang/Class");
		if (!classClass) { ClearPending(env); return nullptr; }
		jmethodID getDeclaredMethods = env->GetMethodID(classClass, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
		env->DeleteLocalRef(classClass);
		if (!getDeclaredMethods) { ClearPending(env); return nullptr; }

		jclass methodClass = env->FindClass("java/lang/reflect/Method");
		if (!methodClass) { ClearPending(env); return nullptr; }
		jmethodID getName = env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;");
		if (!getName) { ClearPending(env); env->DeleteLocalRef(methodClass); return nullptr; }

		jmethodID result = nullptr;
		jclass current = cls;
		while (current && !result)
		{
			jobjectArray methods = (jobjectArray)env->CallObjectMethod(current, getDeclaredMethods);
			if (env->ExceptionCheck()) { env->ExceptionClear(); methods = nullptr; }
			if (methods)
			{
				const jsize count = env->GetArrayLength(methods);
				for (jsize i = 0; i < count && !result; ++i)
				{
					jobject m = env->GetObjectArrayElement(methods, i);
					if (!m) { ClearPending(env); continue; }
					jstring jname = (jstring)env->CallObjectMethod(m, getName);
					if (jname)
					{
						const char* utf = env->GetStringUTFChars(jname, nullptr);
						bool match = false;
						if (utf)
						{
							for (const auto& n : names)
								if (n == utf) { match = true; break; }
						}
						if (match)
							result = env->FromReflectedMethod(m);
						if (utf) env->ReleaseStringUTFChars(jname, utf);
						env->DeleteLocalRef(jname);
					}
					ClearPending(env);
					env->DeleteLocalRef(m);
				}
				env->DeleteLocalRef(methods);
			}
			if (!result)
			{
				jclass parent = env->GetSuperclass(current);
				ClearPending(env);
				if (current != cls) env->DeleteLocalRef(current);
				current = parent;
			}
		}
		if (current && current != cls) env->DeleteLocalRef(current);
		env->DeleteLocalRef(methodClass);
		return result;
	}

	void* ResolveMember(JNIEnv* env, const VapeMapData::Entry* e)
	{
		auto it = g_memberCache.find(e);
		if (it != g_memberCache.end())
			return it->second;
		if (g_failedMembers.count(e))
			return nullptr;

		void* result = nullptr;
		jclass cls = ResolveClass(env, e->owner);
		if (cls)
		{
			const std::vector<std::string> names = SplitNames(e->names);
			std::string sig = SignatureRemap::Apply(e->sig);
			if (sig.empty())
				sig = g_notchEnv ? RemapSignature(e->sig) : (e->sig ? e->sig : "");
			const bool hasSig = e->sig && *e->sig;

			if (hasSig)
			{
				for (const std::string& n : names)
				{
					if (e->kind == 0) // field
					{
						if (e->isStatic)
						{
							result = env->GetStaticFieldID(cls, n.c_str(), sig.c_str());
							if (env->ExceptionCheck()) { env->ExceptionClear(); result = nullptr; }
						}
						if (!result)
						{
							result = env->GetFieldID(cls, n.c_str(), sig.c_str());
							if (env->ExceptionCheck()) { env->ExceptionClear(); result = nullptr; }
						}
						if (!result && e->isStatic)
						{
							result = env->GetStaticFieldID(cls, n.c_str(), sig.c_str());
							if (env->ExceptionCheck()) { env->ExceptionClear(); result = nullptr; }
						}
					}
					else // method or ctor
					{
						if (e->isStatic)
						{
							result = env->GetStaticMethodID(cls, n.c_str(), sig.c_str());
							if (env->ExceptionCheck()) { env->ExceptionClear(); result = nullptr; }
						}
						if (!result)
						{
							result = env->GetMethodID(cls, n.c_str(), sig.c_str());
							if (env->ExceptionCheck()) { env->ExceptionClear(); result = nullptr; }
						}
					}
					if (result) break;
				}

				// name-based lookup exhausted: methods can still be found by descriptor
				if (!result && e->kind == 1)
					result = JniResolve::MethodByDescriptor(env, cls, sig.c_str());
			}
			else
			{
				// no trustworthy signature: match declared members by candidate names
				result = (e->kind == 0)
					? (void*)FieldByNameReflection(env, cls, names)
					: (void*)MethodByNameReflection(env, cls, names);
			}
		}

		if (result)
			g_memberCache.emplace(e, result);
		else
		{
			g_failedMembers.insert(e);
			Logger::Err(std::string("[MappedRegistry] unresolved: ") + e->owner + " -> " + e->names);
		}
		return result;
	}

	const VapeMapData::Entry* FindEntry(const char* owner, const char* name, uint8_t kind,
		const char* sigFilter)
	{
		if (!owner || !name)
			return nullptr;
		auto it = g_byOwner.find(owner);
		if (it == g_byOwner.end())
			return nullptr;
		for (const VapeMapData::Entry* e : it->second)
		{
			if (e->kind != kind)
				continue;
			// primary name is the first token of the names list
			const char* bar = strchr(e->names, '|');
			const size_t plen = bar ? (size_t)(bar - e->names) : strlen(e->names);
			if (strlen(name) != plen || strncmp(e->names, name, plen) != 0)
				continue;
			if (sigFilter && *sigFilter)
			{
				if (!e->sig || strcmp(e->sig, sigFilter) != 0)
					continue;
			}
			return e;
		}
		return nullptr;
	}
}

void MappedRegistry::Init()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_ready)
		return;

	g_byOwner.clear();
	g_notchClass.clear();

	for (unsigned int i = 0; i < VapeMapData::kEntryCount; ++i)
	{
		const VapeMapData::Entry& e = VapeMapData::kEntries[i];
		g_byOwner[e.owner].push_back(&e);

		// feed extracted SRG/Notch aliases into the shared alias table so
		// StrayCache-style name lookups benefit from the same data.
		// k starts at 0 on purpose: the primary MCP name must stay in its own
		// candidate list, otherwise runtimes that ship MCP-named members (Lunar)
		// lose their only working name the moment this entry registers.
		const std::vector<std::string> names = SplitNames(e.names);
		for (size_t k = 0; k < names.size(); ++k)
			MappingLoader::AddAlias(names[0].c_str(), names[k].c_str());
	}

	for (unsigned int i = 0; i < VapeMapData::kClassAliasCount; ++i)
		g_notchClass.emplace(VapeMapData::kClassAliases[i].mcp, VapeMapData::kClassAliases[i].notch);

	g_ready = true;
	Logger::Log("[MappedRegistry] indexed " + std::to_string(VapeMapData::kEntryCount) +
		" members across " + std::to_string(g_byOwner.size()) + " classes");
}

void MappedRegistry::Shutdown()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	JNIEnv* env = Java::GetEnv();
	if (env)
	{
		for (auto& pair : g_classCache)
			if (pair.second)
				env->DeleteGlobalRef(pair.second);
	}
	g_classCache.clear();
	g_failedClasses.clear();
	g_memberCache.clear();
	g_failedMembers.clear();
	g_ready = false;
}

jclass MappedRegistry::Class(const char* ownerPath)
{
	if (!g_ready || !ownerPath)
		return nullptr;
	JNIEnv* env = Java::GetEnv();
	if (!env)
		return nullptr;
	std::lock_guard<std::mutex> lock(g_mutex);

	// detect a vanilla (fully obfuscated) runtime once: if the MCP class path
	// fails but its Notch alias resolves, all signature class names must be
	// rewritten before member lookups
	if (!g_notchEnv && g_classCache.empty() && g_failedClasses.empty())
	{
		jclass probe = ResolveClass(env, ownerPath);
		if (!probe)
		{
			auto mcIt = g_notchClass.find("net/minecraft/client/Minecraft");
			if (mcIt != g_notchClass.end())
			{
				jclass mc = nullptr;
				if (Java::AssignClass(ToDotName(mcIt->second.c_str()), mc) && mc)
				{
					g_notchEnv = true;
					env->DeleteGlobalRef(mc);
					Logger::Log("[MappedRegistry] vanilla (Notch) runtime detected");
					probe = ResolveClass(env, ownerPath);
				}
			}
		}
		return probe;
	}
	return ResolveClass(env, ownerPath);
}

jfieldID MappedRegistry::Field(const char* ownerPath, const char* mcpName, const char* sigFilter)
{
	if (!g_ready)
		return nullptr;
	JNIEnv* env = Java::GetEnv();
	if (!env)
		return nullptr;
	std::lock_guard<std::mutex> lock(g_mutex);
	const VapeMapData::Entry* e = FindEntry(ownerPath, mcpName, 0, sigFilter);
	if (!e)
		return nullptr;
	return (jfieldID)ResolveMember(env, e);
}

jmethodID MappedRegistry::Method(const char* ownerPath, const char* mcpName, const char* sigFilter)
{
	if (!g_ready)
		return nullptr;
	JNIEnv* env = Java::GetEnv();
	if (!env)
		return nullptr;
	std::lock_guard<std::mutex> lock(g_mutex);
	const VapeMapData::Entry* e = FindEntry(ownerPath, mcpName, 1, sigFilter);
	if (!e)
		return nullptr;
	return (jmethodID)ResolveMember(env, e);
}

jmethodID MappedRegistry::Ctor(const char* ownerPath, const char* sigFilter)
{
	if (!g_ready)
		return nullptr;
	JNIEnv* env = Java::GetEnv();
	if (!env)
		return nullptr;
	std::lock_guard<std::mutex> lock(g_mutex);
	const VapeMapData::Entry* e = FindEntry(ownerPath, "<init>", 2, sigFilter);
	if (!e)
		return nullptr;
	return (jmethodID)ResolveMember(env, e);
}
