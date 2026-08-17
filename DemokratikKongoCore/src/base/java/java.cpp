#include "java.h"
#include "../sdk/automap/classIndex.h"
#include "../sdk/automap/nameScheme.h"
#include "../sdk/automap/autoMapper.h"
#include "../sdk/automap/automapUtil.h"
#include "../sdk/automap/liveInspector.h"
#include "../util/logger.h"

#include <thread>
#include <chrono>

JavaVM* vm;
jobject classLoader;
jmethodID mid_findClass;

jclass Java::findClass(JNIEnv* p_env, jvmtiEnv* p_tienv, const std::string& path)
{
	if (ClassIndex::Ready())
	{
		if (jclass hit = ClassIndex::Find(path.c_str()))
			return p_env ? static_cast<jclass>(p_env->NewLocalRef(hit)) : hit;
	}

	if (!p_env || !p_tienv)
		return nullptr;

	jint class_count = 0;
	jclass* classes = nullptr;
	jclass foundclass = nullptr;
	if (p_tienv->GetLoadedClasses(&class_count, &classes) != JVMTI_ERROR_NONE)
		return nullptr;

	for (int i = 0; i < class_count; ++i)
	{
		char* signature_buffer = nullptr;
		if (p_tienv->GetClassSignature(classes[i], &signature_buffer, nullptr) == JVMTI_ERROR_NONE && signature_buffer != nullptr)
		{
			std::string signature = signature_buffer;
			p_tienv->Deallocate((unsigned char*)signature_buffer);
			if (signature.length() > 2)
			{
				signature = signature.substr(1);
				signature.pop_back();
				if (signature == path)
					foundclass = (jclass)p_env->NewLocalRef(classes[i]);
			}
		}
		p_env->DeleteLocalRef(classes[i]);
	}
	p_tienv->Deallocate((unsigned char*)classes);
	return foundclass;
}

bool Java::BindLoaderFromClass(jclass cls)
{
	JNIEnv* env = Java::Env;
	if (!env || !cls)
		return false;

	jclass class_Class = env->FindClass("java/lang/Class");
	if (!class_Class)
	{
		if (env->ExceptionCheck()) env->ExceptionClear();
		return false;
	}
	jmethodID mid_getClassLoader = env->GetMethodID(class_Class, "getClassLoader", "()Ljava/lang/ClassLoader;");
	jobject loaderLocal = mid_getClassLoader ? env->CallObjectMethod(cls, mid_getClassLoader) : nullptr;
	if (env->ExceptionCheck()) env->ExceptionClear();
	env->DeleteLocalRef(class_Class);

	if (!loaderLocal)
		return false;

	if (classLoader)
		env->DeleteGlobalRef(classLoader);
	classLoader = env->NewGlobalRef(loaderLocal);
	jclass classLoaderClass = env->GetObjectClass(classLoader);
	mid_findClass = env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
	env->DeleteLocalRef(classLoaderClass);
	env->DeleteLocalRef(loaderLocal);
	return classLoader && mid_findClass;
}

namespace
{
	bool tryLoadViaLoader(JNIEnv* env, jobject loader, jmethodID loadClass, const char* dotted)
	{
		if (!env || !loader || !loadClass || !dotted)
			return false;
		jstring className = env->NewStringUTF(dotted);
		jobject found = env->CallObjectMethod(loader, loadClass, className);
		if (env->ExceptionCheck())
			env->ExceptionClear();
		env->DeleteLocalRef(className);
		if (!found)
			return false;
		env->DeleteLocalRef(found);
		return true;
	}

	void setupClassLoader()
	{
		JNIEnv* env = Java::Env;
		if (!env || !Java::tiEnv)
			return;

		ClassIndex::Build(env, Java::tiEnv);

		jclass minecraft = ClassIndex::Find("net/minecraft/client/Minecraft");
		const char* how = "JVMTI MCP";
		if (!minecraft)
		{
			minecraft = ClassIndex::Find("ave");
			how = "JVMTI Notch ave";
		}
		if (!minecraft)
		{
			jclass local = LiveInspector::FindMinecraftFromClientThread(env);
			if (local)
			{
				const std::string path = LiveInspector::ClassSlashName(env, local);
				jclass global = static_cast<jclass>(env->NewGlobalRef(local));
				env->DeleteLocalRef(local);
				if (!path.empty())
					ClassIndex::Add(path.c_str(), global);
				minecraft = global;
				how = "client-thread";
			}
		}

		if (minecraft && Java::BindLoaderFromClass(minecraft))
		{
			Java::Initialized = true;
			Logger::Log(std::string("[Java] Resolved classloader via ") + how);
			return;
		}

		// Vanilla Notch classes often have a null (bootstrap) loader.
		// Fall back to scanning thread context loaders with MCP and Notch names.
		jclass c_Thread = env->FindClass("java/lang/Thread");
		jclass c_Map = env->FindClass("java/util/Map");
		jclass c_Set = env->FindClass("java/util/Set");
		jclass c_ClassLoader = env->FindClass("java/lang/ClassLoader");
		if (!c_Thread || !c_Map || !c_Set || !c_ClassLoader)
		{
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (minecraft)
				Logger::Log("[Java] Minecraft found but classloader is bootstrap — marking initialized");
			if (minecraft)
				Java::Initialized = true;
			return;
		}

		jmethodID mid_getAllStackTraces = env->GetStaticMethodID(c_Thread, "getAllStackTraces", "()Ljava/util/Map;");
		jmethodID mid_keySet = env->GetMethodID(c_Map, "keySet", "()Ljava/util/Set;");
		jmethodID mid_toArray = env->GetMethodID(c_Set, "toArray", "()[Ljava/lang/Object;");
		jmethodID mid_getContextClassLoader = env->GetMethodID(c_Thread, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
		mid_findClass = env->GetMethodID(c_ClassLoader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");

		jobject obj_stackTracesMap = env->CallStaticObjectMethod(c_Thread, mid_getAllStackTraces);
		jobject obj_threadsSet = obj_stackTracesMap ? env->CallObjectMethod(obj_stackTracesMap, mid_keySet) : nullptr;
		jobjectArray threads = obj_threadsSet ? (jobjectArray)env->CallObjectMethod(obj_threadsSet, mid_toArray) : nullptr;
		const jint szThreads = threads ? env->GetArrayLength(threads) : 0;

		static const char* kProbes[] = {
			"net.minecraft.client.Minecraft",
			"ave",
		};

		for (int i = 0; i < szThreads && !classLoader; i++)
		{
			jobject thread = env->GetObjectArrayElement(threads, i);
			if (!thread) continue;
			jobject classLoaderObj = env->CallObjectMethod(thread, mid_getContextClassLoader);
			if (env->ExceptionCheck()) env->ExceptionClear();
			if (classLoaderObj)
			{
				for (const char* probe : kProbes)
				{
					if (tryLoadViaLoader(env, classLoaderObj, mid_findClass, probe))
					{
						classLoader = env->NewGlobalRef(classLoaderObj);
						break;
					}
				}
				env->DeleteLocalRef(classLoaderObj);
			}
			env->DeleteLocalRef(thread);
		}

		if (threads) env->DeleteLocalRef(threads);
		if (obj_stackTracesMap) env->DeleteLocalRef(obj_stackTracesMap);
		if (obj_threadsSet) env->DeleteLocalRef(obj_threadsSet);
		env->DeleteLocalRef(c_Thread);
		env->DeleteLocalRef(c_Map);
		env->DeleteLocalRef(c_Set);
		env->DeleteLocalRef(c_ClassLoader);

		if (classLoader)
		{
			Java::Initialized = true;
			Logger::Log("[Java] Resolved classloader via Thread Stack Trace scanning!");
		}
		else if (minecraft)
		{
			Java::Initialized = true;
			Logger::Log("[Java] Minecraft indexed without a custom ClassLoader (bootstrap/Notch)");
		}
		else
		{
			Logger::Err("[Java] Failed to resolve Minecraft ClassLoader!");
		}
	}
}

JNIEnv* JNIEnv_ThreadSafe::Get() const
{
	return Java::GetEnv();
}

JNIEnv* Java::GetEnv()
{
	JNIEnv* env = nullptr;
	if (vm)
	{
		jint res = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
		if (res == JNI_EDETACHED)
		{
			if (vm->AttachCurrentThread((void**)&env, nullptr) == JNI_OK)
			{
				std::lock_guard<std::mutex> lock(attachedThreadsMutex);
				attachedThreads.insert(vm);
			}
		}
	}
	return env;
}

void Java::Init()
{
	Java::Initialized = false;
	classLoader = nullptr;
	mid_findClass = nullptr;

	jsize count = 0;
	if (JNI_GetCreatedJavaVMs(&vm, 1, &count) != JNI_OK || count == 0)
	{
		Logger::Err("[Java] No JVM found.");
		return;
	}

	for (int attempt = 0; attempt < 60 && !Java::Initialized; ++attempt)
	{
		JNIEnv* localEnv = nullptr;
		jint res = vm->GetEnv((void**)&localEnv, JNI_VERSION_1_6);
		if (res == JNI_EDETACHED)
			res = vm->AttachCurrentThread((void**)&localEnv, nullptr);
		if (res != JNI_OK || !localEnv)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			continue;
		}

		vm->GetEnv((void**)&Java::tiEnv, JVMTI_VERSION);
		setupClassLoader();

		if (!Java::Initialized)
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	if (!Java::Initialized)
		Logger::Err("[Java] Failed to resolve Minecraft ClassLoader after retries.");
}

void Java::Kill()
{
	JNIEnv* env = Java::GetEnv();
	AutoMapper::Shutdown();
	ClassIndex::Shutdown(env);

	if (classLoader && env)
	{
		env->DeleteGlobalRef(classLoader);
		classLoader = nullptr;
	}

	{
		std::lock_guard<std::mutex> lock(attachedThreadsMutex);
		if (attachedThreads.count(vm))
		{
			vm->DetachCurrentThread();
			attachedThreads.erase(vm);
		}
	}
}

bool Java::AssignClass(std::string name, jclass& out)
{
	if (out)
		return true;

	JNIEnv* env = Java::GetEnv();
	if (!env)
		return false;

	const std::string slash = AutoMapUtil::ToSlash(name.c_str());
	const std::string dot = AutoMapUtil::ToDot(name.c_str());

	auto adopt = [&](jclass src) -> bool {
		if (!src)
			return false;
		out = static_cast<jclass>(env->NewGlobalRef(src));
		return out != nullptr;
	};

	if (AutoMapper::IsReady())
	{
		if (adopt(AutoMapper::Class(slash.c_str())))
			return true;
	}

	if (adopt(ClassIndex::Find(slash.c_str())))
		return true;

	if (const char* notch = NameSchemeDetect::NotchAlias(slash.c_str()))
	{
		if (adopt(ClassIndex::Find(notch)))
			return true;
	}

	if (classLoader && mid_findClass)
	{
		auto tryLoader = [&](const char* dotted) -> bool {
			jstring className = env->NewStringUTF(dotted);
			jobject local = env->CallObjectMethod(classLoader, mid_findClass, className);
			if (env->ExceptionCheck())
				env->ExceptionClear();
			env->DeleteLocalRef(className);
			if (!local)
				return false;
			out = static_cast<jclass>(env->NewGlobalRef(local));
			env->DeleteLocalRef(local);
			return out != nullptr;
		};

		if (tryLoader(dot.c_str()))
			return true;
		if (const char* notch = NameSchemeDetect::NotchAlias(slash.c_str()))
		{
			if (tryLoader(AutoMapUtil::ToDot(notch).c_str()))
				return true;
		}
	}

	return false;
}
