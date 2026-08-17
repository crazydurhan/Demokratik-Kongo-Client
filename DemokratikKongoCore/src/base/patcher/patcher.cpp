#include "patcher.h"
#include "data.h"
#include "../util/logger.h"
#include "../moduleManager/moduleManager.h"
#include "../moduleManager/modules/utility/itemLogger.h"
#include "../moduleManager/modules/combat/itemWhitelist.h"
#include "../base.h"
#include <functional>
#include <chrono>
#include <atomic>
#include <thread>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <Windows.h>
#include "miniz.h"

namespace Patcher
{
	namespace
	{
		std::atomic<bool> s_nativeRegistered{ false };
		std::atomic<int>  s_retransformOkCount{ 0 };
		std::atomic<int>  s_patchSuccessCount{ 0 };
		std::atomic<int>  s_patchFailCount{ 0 };
		std::chrono::steady_clock::time_point s_lastRetryTime{};
		jclass ClassPatcher_class = nullptr;
		jclass RuntimeBridge_class = nullptr;

		// Per-class patch applied bits (ClassFileLoadHook success).
		std::atomic<bool> s_patchedEntityRenderer{ false };
		std::atomic<bool> s_patchedMinecraft{ false };
		std::atomic<bool> s_patchedEntityPlayerSP{ false };
		std::atomic<bool> s_patchedNetworkManager{ false };
		std::atomic<bool> s_patchedPlayerControllerMP{ false };
		std::atomic<bool> s_patchedGuiIngame{ false };
		std::atomic<bool> s_patchedRendererLivingEntity{ false };
		std::atomic<bool> s_patchedRenderPlayer{ false };

		// Retransform OK bits (JVMTI accepted the class).
		std::atomic<bool> s_rtEntityRenderer{ false };
		std::atomic<bool> s_rtMinecraft{ false };
		std::atomic<bool> s_rtEntityPlayerSP{ false };
		std::atomic<bool> s_rtNetworkManager{ false };
		std::atomic<bool> s_rtPlayerControllerMP{ false };
		std::atomic<bool> s_rtGuiIngame{ false };
		std::atomic<bool> s_rtRendererLivingEntity{ false };
		std::atomic<bool> s_rtRenderPlayer{ false };

		std::atomic<long long> s_lastRunTickPreMs{ 0 };

		const char* jvmtiErrorName(jvmtiError err)
		{
			if (err == JVMTI_ERROR_NONE) return "NONE (ok)";
			if (err == JVMTI_ERROR_INVALID_CLASS_FORMAT) return "INVALID_CLASS_FORMAT";
			if (err == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_METHOD_ADDED) return "UNSUPPORTED_REDEFINITION_METHOD_ADDED";
			if (err == JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED) return "UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED";
			if (err == JVMTI_ERROR_INVALID_CLASS) return "INVALID_CLASS";
			if (err == JVMTI_ERROR_UNMODIFIABLE_CLASS) return "UNMODIFIABLE_CLASS";
			if (err == JVMTI_ERROR_FAILS_VERIFICATION) return "FAILS_VERIFICATION";
			return "UNKNOWN";
		}

		void logStatus(const char* context)
		{
			auto flag = [](std::atomic<bool>& a) { return a.load() ? "OK" : "FAIL"; };
			std::ostringstream oss;
			oss << context << " | ready=" << (Patcher::IsReady() ? "yes" : "no")
				<< " natives=" << (s_nativeRegistered.load() ? "yes" : "no")
				<< " retransforms=" << s_retransformOkCount.load()
				<< " bytecode_patches=" << s_patchSuccessCount.load()
				<< " patch_failures=" << s_patchFailCount.load()
				<< " ClassPatcher=" << (ClassPatcher_class ? "loaded" : "missing")
				<< " RuntimeBridge=" << (RuntimeBridge_class ? "loaded" : "missing");
			Logger::Info("Patcher", oss.str());

			std::ostringstream cls;
			cls << "Class status (rt/patch):"
				<< " Minecraft=" << flag(s_rtMinecraft) << "/" << flag(s_patchedMinecraft)
				<< " EntityRenderer=" << flag(s_rtEntityRenderer) << "/" << flag(s_patchedEntityRenderer)
				<< " EntityPlayerSP=" << flag(s_rtEntityPlayerSP) << "/" << flag(s_patchedEntityPlayerSP)
				<< " NetworkManager=" << flag(s_rtNetworkManager) << "/" << flag(s_patchedNetworkManager)
				<< " PlayerControllerMP=" << flag(s_rtPlayerControllerMP) << "/" << flag(s_patchedPlayerControllerMP)
				<< " GuiIngame=" << flag(s_rtGuiIngame) << "/" << flag(s_patchedGuiIngame)
				<< " RendererLivingEntity=" << flag(s_rtRendererLivingEntity) << "/" << flag(s_patchedRendererLivingEntity)
				<< " RenderPlayer=" << flag(s_rtRenderPlayer) << "/" << flag(s_patchedRenderPlayer);
			Logger::Info("Patcher", cls.str());

			const long long lastRt = s_lastRunTickPreMs.load(std::memory_order_acquire);
			if (lastRt == 0)
				Logger::Warn("Patcher", "OnRunTickPre heartbeat: never fired (runTick / getMouseOver drain idle)");
			else
			{
				const long long age = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count() - lastRt;
				if (age > 2000)
					Logger::Warn("Patcher", "OnRunTickPre heartbeat stale ageMs=" + std::to_string(age));
				else
					Logger::Info("Patcher", "OnRunTickPre heartbeat ageMs=" + std::to_string(age));
			}
			if (!s_rtMinecraft.load(std::memory_order_acquire) && s_rtEntityRenderer.load(std::memory_order_acquire))
				Logger::Info("Patcher", "Minecraft retransform failed — using EntityRenderer.getMouseOver OnRunTickPre fallback");
			if (!s_rtEntityPlayerSP.load(std::memory_order_acquire))
				Logger::Warn("Patcher", "EntityPlayerSP retransform failed — silent rotation / walking hooks inactive");
		}

		void markPatched(const char* shortName)
		{
			if (!shortName) return;
			if (strcmp(shortName, "Minecraft") == 0) s_patchedMinecraft.store(true, std::memory_order_release);
			else if (strcmp(shortName, "EntityRenderer") == 0) s_patchedEntityRenderer.store(true, std::memory_order_release);
			else if (strcmp(shortName, "EntityPlayerSP") == 0) s_patchedEntityPlayerSP.store(true, std::memory_order_release);
			else if (strcmp(shortName, "NetworkManager") == 0) s_patchedNetworkManager.store(true, std::memory_order_release);
			else if (strcmp(shortName, "PlayerControllerMP") == 0) s_patchedPlayerControllerMP.store(true, std::memory_order_release);
			else if (strcmp(shortName, "GuiIngame") == 0) s_patchedGuiIngame.store(true, std::memory_order_release);
			else if (strcmp(shortName, "RendererLivingEntity") == 0) s_patchedRendererLivingEntity.store(true, std::memory_order_release);
			else if (strcmp(shortName, "RenderPlayer") == 0) s_patchedRenderPlayer.store(true, std::memory_order_release);
		}

		void markRetransform(const char* name, bool ok)
		{
			if (!name) return;
			auto set = [&](std::atomic<bool>& a) { a.store(ok, std::memory_order_release); };
			if (strstr(name, "EntityRenderer")) set(s_rtEntityRenderer);
			else if (strstr(name, "Minecraft")) set(s_rtMinecraft);
			else if (strstr(name, "EntityPlayerSP")) set(s_rtEntityPlayerSP);
			else if (strstr(name, "NetworkManager")) set(s_rtNetworkManager);
			else if (strstr(name, "PlayerControllerMP")) set(s_rtPlayerControllerMP);
			else if (strstr(name, "GuiIngame")) set(s_rtGuiIngame);
			else if (strstr(name, "RendererLivingEntity")) set(s_rtRendererLivingEntity);
			else if (strstr(name, "RenderPlayer")) set(s_rtRenderPlayer);
		}

		// Dump original + patched class bytes once per class so INVALID_CLASS_FORMAT
		// rejections can be analyzed offline (ASM CheckClassAdapter / javap) without the game.
		void dumpClassPairOnce(const char* className, const unsigned char* orig, jint origLen,
			const unsigned char* patched, jint patchedLen)
		{
			if (!className || !orig || !patched || origLen <= 0 || patchedLen <= 0)
				return;

			static std::unordered_set<std::string> s_dumped;
			if (s_dumped.count(className))
				return;
			s_dumped.insert(className);

			char tempPath[MAX_PATH]{};
			if (GetTempPathA(MAX_PATH, tempPath) == 0)
				return;

			std::string dir = std::string(tempPath) + "dk_dumps\\";
			CreateDirectoryA(dir.c_str(), nullptr);

			auto writeFile = [&](const char* suffix, const unsigned char* buf, jint len)
			{
				std::string path = dir + className + suffix;
				HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
					CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (h == INVALID_HANDLE_VALUE)
					return;
				DWORD written = 0;
				WriteFile(h, buf, (DWORD)len, &written, nullptr);
				CloseHandle(h);
			};

			writeFile("_orig.class", orig, origLen);
			writeFile("_patched.class", patched, patchedLen);
			Logger::Info("Patcher", std::string("Dumped class bytes -> ") + dir + className + "_{orig,patched}.class");
		}

		void JNICALL logFromJava(JNIEnv* env, jclass clazz, jstring level, jstring message)
		{
			(void)clazz;
			if (!level || !message) return;
			const char* lvl = env->GetStringUTFChars(level, nullptr);
			const char* msg = env->GetStringUTFChars(message, nullptr);
			if (!lvl || !msg) return;

			std::string text = msg;
			if (strcmp(lvl, "ERROR") == 0) Logger::Error("Java", text);
			else if (strcmp(lvl, "WARN") == 0) Logger::Warn("Java", text);
			else if (strcmp(lvl, "DEBUG") == 0) Logger::Debug("Java", text);
			else Logger::Info("Java", text);

			env->ReleaseStringUTFChars(level, lvl);
			env->ReleaseStringUTFChars(message, msg);
		}

		// Capture pending JNI exception into our crash log (ExceptionDescribe only hits stderr).
		void logAndClearException(JNIEnv* env, const char* context)
		{
			if (!env->ExceptionCheck())
				return;

			jthrowable exc = env->ExceptionOccurred();
			env->ExceptionClear();
			std::string detail = context ? context : "JNI exception";
			if (exc)
			{
				jclass throwableCls = env->FindClass("java/lang/Throwable");
				if (throwableCls)
				{
					jmethodID toStringMid = env->GetMethodID(throwableCls, "toString", "()Ljava/lang/String;");
					if (toStringMid)
					{
						jstring jmsg = (jstring)env->CallObjectMethod(exc, toStringMid);
						if (jmsg && !env->ExceptionCheck())
						{
							const char* utf = env->GetStringUTFChars(jmsg, nullptr);
							if (utf)
							{
								detail += ": ";
								detail += utf;
								env->ReleaseStringUTFChars(jmsg, utf);
							}
							env->DeleteLocalRef(jmsg);
						}
						else if (env->ExceptionCheck())
							env->ExceptionClear();
					}
					env->DeleteLocalRef(throwableCls);
				}
				env->DeleteLocalRef(exc);
			}
			Logger::Error("Patcher", detail);
		}

	void JNICALL pushItemLog(JNIEnv* env, jclass clazz, jstring message)
	{
		(void)clazz;
		if (!message) return;
		const char* msg = env->GetStringUTFChars(message, nullptr);
		if (!msg) return;
		ItemLogger::PushLog(msg);
		env->ReleaseStringUTFChars(message, msg);
	}

	// ItemLogger ground/pickup matching through the shared C++ ItemWhitelist,
	// so its whitelist accepts the exact same syntax as the combat modules.
	jboolean JNICALL itemWhitelistMatch(JNIEnv* env, jclass clazz,
		jstring name, jint itemId, jint itemMeta, jboolean isBlock, jstring whitelist)
	{
		(void)clazz;
		if (!whitelist) return JNI_FALSE;

		const char* wlChars = env->GetStringUTFChars(whitelist, nullptr);
		if (!wlChars) return JNI_FALSE;
		std::string wl(wlChars);
		env->ReleaseStringUTFChars(whitelist, wlChars);

		std::string nm;
		if (name)
		{
			const char* nmChars = env->GetStringUTFChars(name, nullptr);
			if (nmChars)
			{
				nm = nmChars;
				env->ReleaseStringUTFChars(name, nmChars);
			}
		}

		return ItemWhitelist::MatchesItem(nm, (int)itemId, (int)itemMeta,
			isBlock == JNI_TRUE, wl) ? JNI_TRUE : JNI_FALSE;
	}

		std::string toFileUrl(const std::string& path)
		{
			std::string url = "file:///";
			for (char c : path)
				url += (c == '\\') ? '/' : c;
			return url;
		}

		std::string getModuleDirectory()
		{
			char path[MAX_PATH]{};
			HMODULE self = nullptr;
			if (!GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(&getModuleDirectory), &self))
				return {};

			const DWORD len = GetModuleFileNameA(self, path, MAX_PATH);
			if (len == 0 || len >= MAX_PATH)
				return {};

			std::string dir(path, len);
			const size_t slash = dir.find_last_of("\\/");
			if (slash == std::string::npos)
				return {};
			return dir.substr(0, slash + 1);
		}

		bool writeFileBytes(const std::string& path, const unsigned char* bytes, size_t size)
		{
			DeleteFileA(path.c_str());

			HANDLE h = CreateFileA(
				path.c_str(),
				GENERIC_WRITE,
				FILE_SHARE_READ,
				nullptr,
				CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);
			if (h == INVALID_HANDLE_VALUE)
				return false;

			DWORD written = 0;
			const BOOL ok = WriteFile(h, bytes, static_cast<DWORD>(size), &written, nullptr);
			CloseHandle(h);
			return ok && written == size;
		}

		std::string writeEmbeddedJarToTemp(const unsigned char* jarBytes, size_t size)
		{
			const DWORD pid = GetCurrentProcessId();
			std::vector<std::string> candidates;

			char tempDir[MAX_PATH]{};
			if (GetTempPathA(MAX_PATH, tempDir) != 0)
			{
				candidates.push_back(std::string(tempDir) + "projectx_patcher_" + std::to_string(pid) + ".jar");
				candidates.push_back(std::string(tempDir) + "projectx_patcher.jar");
			}

			char localAppData[MAX_PATH]{};
			if (GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH) > 0)
			{
				std::string dir = std::string(localAppData) + "\\ProjectX\\";
				CreateDirectoryA((std::string(localAppData) + "\\ProjectX").c_str(), nullptr);
				candidates.push_back(dir + "patcher_" + std::to_string(pid) + ".jar");
			}

			const std::string dllDir = getModuleDirectory();
			if (!dllDir.empty())
				candidates.push_back(dllDir + "projectx_patcher_" + std::to_string(pid) + ".jar");

			// Quick sanity check: fat jar must contain core ASM classes.
			bool hasAsmCore = false;
			for (size_t i = 0; i + 28 < size; ++i)
			{
				if (memcmp(jarBytes + i, "org/objectweb/asm/ClassWriter.class", 28) == 0)
				{
					hasAsmCore = true;
					break;
				}
			}
			if (!hasAsmCore)
				Logger::Error("Patcher", "Embedded jar missing org/objectweb/asm/ClassWriter.class — rebuild asm/build_patcher.ps1");
			else if (size < 153000)
				Logger::Warn("Patcher", "Embedded jar looks stale (" + std::to_string(size)
					+ " bytes) — rebuild asm and regenerate data.h for COMPUTE_FRAMES fixes");

			for (const std::string& path : candidates)
			{
				if (!writeFileBytes(path, jarBytes, size))
				{
					Logger::Warn("Patcher", "Jar write failed: " + path + " (Win32=" + std::to_string(GetLastError()) + ")");
					continue;
				}

				Logger::Info("Patcher", "Temp jar ready: " + path + " (" + std::to_string(size) + " bytes)");
				return path;
			}

			Logger::Error("Patcher", "Cannot write embedded patcher jar to any candidate path (Win32="
				+ std::to_string(GetLastError()) + ")");
			return {};
		}

		bool loadClassViaLoader(JNIEnv* env, jobject classLoader, const char* dottedName)
		{
			jclass loaderClass = env->GetObjectClass(classLoader);
			if (!loaderClass) return false;

			jmethodID loadClassMid = env->GetMethodID(loaderClass, "loadClass",
				"(Ljava/lang/String;)Ljava/lang/Class;");
			env->DeleteLocalRef(loaderClass);
			if (!loadClassMid) return false;

			jstring className = env->NewStringUTF(dottedName);
			jobject clazzObj = env->CallObjectMethod(classLoader, loadClassMid, className);
			env->DeleteLocalRef(className);

			if (env->ExceptionCheck())
			{
				logAndClearException(env, (std::string("URLClassLoader.loadClass failed: ") + dottedName).c_str());
				return false;
			}

			if (clazzObj)
				env->DeleteLocalRef(clazzObj);
			return true;
		}

		void setContextClassLoader(JNIEnv* env, jobject classLoader)
		{
			if (!classLoader) return;

			jclass threadClass = env->FindClass("java/lang/Thread");
			if (!threadClass) return;

			jmethodID currentThreadMid = env->GetStaticMethodID(threadClass, "currentThread", "()Ljava/lang/Thread;");
			jmethodID setContextMid = env->GetMethodID(threadClass, "setContextClassLoader", "(Ljava/lang/ClassLoader;)V");
			if (!currentThreadMid || !setContextMid)
			{
				env->DeleteLocalRef(threadClass);
				return;
			}

			jobject thread = env->CallStaticObjectMethod(threadClass, currentThreadMid);
			if (thread && !env->ExceptionCheck())
				env->CallVoidMethod(thread, setContextMid, classLoader);

			if (env->ExceptionCheck())
			{
				Logger::Warn("Patcher", "setContextClassLoader failed");
				env->ExceptionClear();
			}

			if (thread)
				env->DeleteLocalRef(thread);
			env->DeleteLocalRef(threadClass);
		}

		bool preloadAsmClasses(JNIEnv* env, jobject classLoader)
		{
			static const char* kAsmClasses[] = {
				"org.objectweb.asm.Opcodes",
				"org.objectweb.asm.Type",
				"org.objectweb.asm.Handle",
				"org.objectweb.asm.Label",
				"org.objectweb.asm.Frame",
				"org.objectweb.asm.CurrentFrame",
				"org.objectweb.asm.ClassReader",
				"org.objectweb.asm.ClassVisitor",
				"org.objectweb.asm.MethodVisitor",
				"org.objectweb.asm.ClassWriter",
			};

			bool ok = true;
			for (const char* cn : kAsmClasses)
			{
				if (!loadClassViaLoader(env, classLoader, cn))
					ok = false;
			}

			if (ok)
				Logger::Info("Patcher", "ASM9 classpath verified via URLClassLoader");
			else
				Logger::Error("Patcher", "ASM9 preload incomplete — bytecode patching may fail");
			return ok;
		}

		bool bootstrapPatcherClasses(JNIEnv* env, jobject classLoader)
		{
			setContextClassLoader(env, classLoader);

			if (!preloadAsmClasses(env, classLoader))
				return false;

			if (!loadClassViaLoader(env, classLoader, "io.github.lefraudeur.ClassPatcher"))
			{
				Logger::Error("Patcher", "URLClassLoader.loadClass(ClassPatcher) failed");
				return false;
			}

			Logger::Info("Patcher", "ClassPatcher + ASM9 loaded via URLClassLoader");
			return true;
		}

		jclass loadClassPatcher(JNIEnv* env)
		{
			if (!patcherClassLoader)
				return nullptr;

			jclass loaderClass = env->GetObjectClass(patcherClassLoader);
			if (!loaderClass)
				return nullptr;

			jmethodID loadClassMid = env->GetMethodID(loaderClass, "loadClass",
				"(Ljava/lang/String;)Ljava/lang/Class;");
			env->DeleteLocalRef(loaderClass);
			if (!loadClassMid)
				return nullptr;

			jstring className = env->NewStringUTF("io.github.lefraudeur.ClassPatcher");
			jobject clazzObj = env->CallObjectMethod(patcherClassLoader, loadClassMid, className);
			env->DeleteLocalRef(className);
			if (env->ExceptionCheck())
			{
				logAndClearException(env, "loadClass(io.github.lefraudeur.ClassPatcher) failed");
				return nullptr;
			}
			return (jclass)clazzObj;
		}

		bool verifyPatchMethod(JNIEnv* env, jclass clazz, const char* name)
		{
			jmethodID id = env->GetStaticMethodID(clazz, name,
				"([BLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)[B");
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
				id = nullptr;
			}
			if (id)
				Logger::Debug("Patcher", std::string("Verified patch method: ") + name);
			else
				Logger::Error("Patcher", std::string("Missing patch method: ") + name);
			return id != nullptr;
		}

		bool resolveClassPatcher(JNIEnv* env)
		{
			if (ClassPatcher_class)
				return true;

			jclass local = loadClassPatcher(env);
			if (!local)
				return false;

			bool ok = true;
			ok &= verifyPatchMethod(env, local, "patchEntityRenderer");
			ok &= verifyPatchMethod(env, local, "patchRendererLivingEntity");
			ok &= verifyPatchMethod(env, local, "patchMinecraft");
			ok &= verifyPatchMethod(env, local, "patchEntityPlayerSP");
			ok &= verifyPatchMethod(env, local, "patchGuiIngame");
			ok &= verifyPatchMethod(env, local, "patchNetworkManager");
			ok &= verifyPatchMethod(env, local, "patchPlayerControllerMP");

			if (!ok)
			{
				env->DeleteLocalRef(local);
				return false;
			}

			ClassPatcher_class = (jclass)env->NewGlobalRef(local);
			env->DeleteLocalRef(local);
			Logger::Info("Patcher", "ClassPatcher resolved (EntityRenderer + Nametags + EntityPlayerSP patches available)");
			return true;
		}

		bool extractJarEntry(const unsigned char* jarBytes, size_t jarSize, const char* entryName, std::vector<unsigned char>& out)
		{
			mz_zip_archive zip{};
			if (!mz_zip_reader_init_mem(&zip, jarBytes, jarSize, 0))
				return false;

			const int idx = mz_zip_reader_locate_file(&zip, entryName, nullptr, 0);
			if (idx < 0)
			{
				mz_zip_reader_end(&zip);
				return false;
			}

			size_t uncompSize = 0;
			void* p = mz_zip_reader_extract_to_heap(&zip, idx, &uncompSize, 0);
			mz_zip_reader_end(&zip);
			if (!p)
				return false;

			out.assign(static_cast<unsigned char*>(p), static_cast<unsigned char*>(p) + uncompSize);
			mz_free(p);
			return true;
		}

		jobject getClassLoaderFor(JNIEnv* env, jclass clazz)
		{
			if (!clazz) return nullptr;

			jclass classCls = env->GetObjectClass(clazz);
			if (!classCls) return nullptr;

			jmethodID mid = env->GetMethodID(classCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
			jobject loader = mid ? env->CallObjectMethod(clazz, mid) : nullptr;
			env->DeleteLocalRef(classCls);

			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
				return nullptr;
			}
			return loader;
		}

		bool createSingleClassJar(const std::string& path, const char* entryName,
			const void* entryData, size_t entrySize)
		{
			DeleteFileA(path.c_str());

			mz_zip_archive zip{};
			memset(&zip, 0, sizeof(zip));
			if (!mz_zip_writer_init_file(&zip, path.c_str(), 0))
				return false;

			const mz_bool added = mz_zip_writer_add_mem(
				&zip, entryName, entryData, entrySize, MZ_DEFAULT_COMPRESSION);
			const mz_bool finalized = added && mz_zip_writer_finalize_archive(&zip);
			const mz_bool ended = finalized && mz_zip_writer_end(&zip);
			if (!ended)
				mz_zip_writer_end(&zip);
			return ended == MZ_TRUE;
		}

		std::string writeRuntimeBridgeJarToTemp(const std::vector<unsigned char>& classBytes)
		{
			const DWORD pid = GetCurrentProcessId();
			std::vector<std::string> candidates;

			char tempDir[MAX_PATH]{};
			if (GetTempPathA(MAX_PATH, tempDir) != 0)
				candidates.push_back(std::string(tempDir) + "projectx_bridge_" + std::to_string(pid) + ".jar");

			char localAppData[MAX_PATH]{};
			if (GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH) > 0)
			{
				CreateDirectoryA((std::string(localAppData) + "\\ProjectX").c_str(), nullptr);
				candidates.push_back(std::string(localAppData) + "\\ProjectX\\bridge_" + std::to_string(pid) + ".jar");
			}

			const std::string dllDir = getModuleDirectory();
			if (!dllDir.empty())
				candidates.push_back(dllDir + "projectx_bridge_" + std::to_string(pid) + ".jar");

			for (const std::string& path : candidates)
			{
				if (createSingleClassJar(path, "io/github/lefraudeur/RuntimeBridge.class",
					classBytes.data(), classBytes.size()))
					return path;
			}
			return {};
		}

		jclass loadRuntimeBridgeViaForName(JNIEnv* env)
		{
			jclass classClass = env->FindClass("java/lang/Class");
			if (!classClass)
				return nullptr;

			jmethodID forName = env->GetStaticMethodID(classClass, "forName",
				"(Ljava/lang/String;)Ljava/lang/Class;");
			if (!forName)
			{
				env->DeleteLocalRef(classClass);
				return nullptr;
			}

			jstring name = env->NewStringUTF("io.github.lefraudeur.RuntimeBridge");
			jobject clsObj = env->CallStaticObjectMethod(classClass, forName, name);
			env->DeleteLocalRef(name);
			env->DeleteLocalRef(classClass);

			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
				return nullptr;
			}
			return (jclass)clsObj;
		}

		bool defineClassViaReflection(JNIEnv* env, jobject loader, const char* dottedName,
			const std::vector<unsigned char>& bytes, jclass& out)
		{
			jclass loaderClass = env->GetObjectClass(loader);
			if (!loaderClass)
				return false;

			jmethodID defineClass = env->GetMethodID(loaderClass, "defineClass",
				"(Ljava/lang/String;[BII)Ljava/lang/Class;");
			env->DeleteLocalRef(loaderClass);
			if (!defineClass)
				return false;

			jstring name = env->NewStringUTF(dottedName);
			jbyteArray arr = env->NewByteArray(static_cast<jsize>(bytes.size()));
			env->SetByteArrayRegion(arr, 0, static_cast<jsize>(bytes.size()),
				reinterpret_cast<const jbyte*>(bytes.data()));

			jobject clsObj = env->CallObjectMethod(loader, defineClass, name, arr, 0,
				static_cast<jint>(bytes.size()));
			env->DeleteLocalRef(name);
			env->DeleteLocalRef(arr);

			if (env->ExceptionCheck() || !clsObj)
			{
				if (env->ExceptionCheck())
				{
					Logger::Error("Patcher", std::string("ClassLoader.defineClass failed for ") + dottedName);
					env->ExceptionDescribe();
					env->ExceptionClear();
				}
				return false;
			}

			out = (jclass)clsObj;
			return true;
		}

		bool loadClassOnLoader(JNIEnv* env, jobject loader, const char* dottedName, jclass& out)
		{
			out = nullptr;
			if (!loader) return false;

			jclass loaderClass = env->GetObjectClass(loader);
			if (!loaderClass) return false;

			jmethodID loadClass = env->GetMethodID(loaderClass, "loadClass",
				"(Ljava/lang/String;)Ljava/lang/Class;");
			env->DeleteLocalRef(loaderClass);
			if (!loadClass) return false;

			jstring name = env->NewStringUTF(dottedName);
			jobject clsObj = env->CallObjectMethod(loader, loadClass, name);
			env->DeleteLocalRef(name);

			if (env->ExceptionCheck() || !clsObj)
			{
				if (env->ExceptionCheck()) env->ExceptionClear();
				return false;
			}

			out = (jclass)clsObj;
			return true;
		}

		bool isRuntimeBridgeVisibleToMcLoader(JNIEnv* env)
		{
			if (!EntityRenderer_class) return false;

			jobject mcLoader = getClassLoaderFor(env, EntityRenderer_class);
			if (!mcLoader) return false;

			jclass local = nullptr;
			const bool ok = loadClassOnLoader(env, mcLoader, "io.github.lefraudeur.RuntimeBridge", local);
			if (local) env->DeleteLocalRef(local);
			env->DeleteLocalRef(mcLoader);
			return ok;
		}

		void bindRuntimeBridgeMcLoader(JNIEnv* env)
		{
			if (!RuntimeBridge_class || !EntityRenderer_class) return;

			jobject mcLoader = getClassLoaderFor(env, EntityRenderer_class);
			if (!mcLoader) return;

			jmethodID setLoaderMid = env->GetStaticMethodID(RuntimeBridge_class,
				"setMinecraftClassLoader", "(Ljava/lang/ClassLoader;)V");
			if (setLoaderMid)
			{
				env->CallStaticVoidMethod(RuntimeBridge_class, setLoaderMid, mcLoader);
				if (env->ExceptionCheck())
				{
					env->ExceptionDescribe();
					env->ExceptionClear();
				}
			}
			env->DeleteLocalRef(mcLoader);
		}

		bool injectEmbeddedClassViaJava(JNIEnv* env, jobject mcLoader,
			const char* jarEntry, const char* binaryName)
		{
			if (!ClassPatcher_class || !mcLoader || !jarEntry || !binaryName)
				return false;

			std::vector<unsigned char> bytes;
			if (!extractJarEntry(data, sizeof(data), jarEntry, bytes))
			{
				Logger::Warn("Patcher", std::string("Missing embedded class: ") + jarEntry);
				return false;
			}

			jmethodID injectMid = env->GetStaticMethodID(ClassPatcher_class,
				"injectClassToLoader", "(Ljava/lang/ClassLoader;Ljava/lang/String;[B)Z");
			if (!injectMid)
				return false;

			jstring jname = env->NewStringUTF(binaryName);
			jbyteArray arr = env->NewByteArray(static_cast<jsize>(bytes.size()));
			env->SetByteArrayRegion(arr, 0, static_cast<jsize>(bytes.size()),
				reinterpret_cast<const jbyte*>(bytes.data()));

			jboolean ok = env->CallStaticBooleanMethod(
				ClassPatcher_class, injectMid, mcLoader, jname, arr);

			if (env->ExceptionCheck())
			{
				Logger::Error("Patcher", std::string("injectClassToLoader threw for ") + binaryName);
				env->ExceptionDescribe();
				env->ExceptionClear();
				ok = JNI_FALSE;
			}

			env->DeleteLocalRef(jname);
			env->DeleteLocalRef(arr);

			if (ok)
				Logger::Info("Patcher", std::string(binaryName) + " injected on MC classloader");
			else
				Logger::Warn("Patcher", std::string("Failed to inject ") + binaryName + " on MC classloader");

			return ok == JNI_TRUE;
		}

		void ensureRuntimeBridgeInnersInjected(JNIEnv* env)
		{
			if (!EntityRenderer_class)
				return;

			jobject mcLoader = getClassLoaderFor(env, EntityRenderer_class);
			if (!mcLoader)
				return;

			// RuntimeBridge.nt_renderWorld instantiates NtCacheEntry every frame.
			// Only the outer RuntimeBridge.class was defined on the MC classloader,
			// so the nametag draw pass died with NoClassDefFoundError while the
			// vanilla cancel hook (no inner-class use) kept working — nametags
			// disappeared and were never redrawn.
			static const struct { const char* entry; const char* binary; } kRuntimeBridgeInnerClasses[] = {
				{ "io/github/lefraudeur/RuntimeBridge$NtCacheEntry.class", "io.github.lefraudeur.RuntimeBridge$NtCacheEntry" },
			};
			for (const auto& cls : kRuntimeBridgeInnerClasses)
				injectEmbeddedClassViaJava(env, mcLoader, cls.entry, cls.binary);
			env->DeleteLocalRef(mcLoader);
		}

		void ensureEspBridgeInjected(JNIEnv* env)
		{
			if (!EntityRenderer_class)
				return;

			jobject mcLoader = getClassLoaderFor(env, EntityRenderer_class);
			if (!mcLoader)
				return;

			// Outer + inner classes must both be defined on the MC classloader.
			// Defining only EspBridge.class causes NoClassDefFoundError for EspBridge$ScreenBox
			// when 2D/Outline ESP projects screen boxes.
			static const struct { const char* entry; const char* binary; } kEspBridgeClasses[] = {
				{ "io/github/lefraudeur/EspBridge.class", "io.github.lefraudeur.EspBridge" },
				{ "io/github/lefraudeur/EspBridge$ScreenBox.class", "io.github.lefraudeur.EspBridge$ScreenBox" },
				{ "io/github/lefraudeur/EspBridge$ItemLabel.class", "io.github.lefraudeur.EspBridge$ItemLabel" },
				{ "io/github/lefraudeur/EspBridge$DmgTag.class", "io.github.lefraudeur.EspBridge$DmgTag" },
				{ "io/github/lefraudeur/EspBridge$IndicatorMark.class", "io.github.lefraudeur.EspBridge$IndicatorMark" },
				{ "io/github/lefraudeur/DummyScreen.class", "io.github.lefraudeur.DummyScreen" },
			};
			for (const auto& cls : kEspBridgeClasses)
				injectEmbeddedClassViaJava(env, mcLoader, cls.entry, cls.binary);
			env->DeleteLocalRef(mcLoader);
		}

		void ensureLagBridgeInjected(JNIEnv* env)
		{
			if (!EntityRenderer_class)
				return;

			jobject mcLoader = getClassLoaderFor(env, EntityRenderer_class);
			if (!mcLoader)
				return;

			// NetworkManager runs on the MC classloader and calls LagBridge directly.
			// Outer + all inner classes must be defined there or channelRead0 crashes
			// with NoClassDefFoundError: io/github/lefraudeur/LagBridge.
			static const struct { const char* entry; const char* binary; } kLagBridgeClasses[] = {
				{ "io/github/lefraudeur/LagBridge.class", "io.github.lefraudeur.LagBridge" },
			{ "io/github/lefraudeur/LagBridge$QueuedPacket.class", "io.github.lefraudeur.LagBridge$QueuedPacket" },
			{ "io/github/lefraudeur/LagBridge$OutSlot.class", "io.github.lefraudeur.LagBridge$OutSlot" },
			{ "io/github/lefraudeur/LagBridge$1.class", "io.github.lefraudeur.LagBridge$1" },
			{ "io/github/lefraudeur/ItemLoggerBridge.class", "io.github.lefraudeur.ItemLoggerBridge" },
			{ "io/github/lefraudeur/ItemLoggerBridge$SavedItemData.class", "io.github.lefraudeur.ItemLoggerBridge$SavedItemData" },
			// LagBridge.onLagTick() calls FreecamBridge.onTick() — without defining
			// it here the tick died every frame with NoClassDefFoundError and
			// Freecam never ran.
			{ "io/github/lefraudeur/FreecamBridge.class", "io.github.lefraudeur.FreecamBridge" },
		};
			for (const auto& cls : kLagBridgeClasses)
				injectEmbeddedClassViaJava(env, mcLoader, cls.entry, cls.binary);
			env->DeleteLocalRef(mcLoader);
		}

		bool finishRuntimeBridgeInjection(JNIEnv* env, jobject mcLoader, jclass local, const char* logMsg)
		{
			RuntimeBridge_class = (jclass)env->NewGlobalRef(local);
			env->DeleteLocalRef(local);
			bindRuntimeBridgeMcLoader(env);
			ensureRuntimeBridgeInnersInjected(env);
			ensureEspBridgeInjected(env);
			ensureLagBridgeInjected(env);
			env->DeleteLocalRef(mcLoader);
			if (logMsg)
				Logger::Info("Patcher", logMsg);
			return true;
		}

		bool injectRuntimeBridge(JNIEnv* env)
		{
			if (RuntimeBridge_class && isRuntimeBridgeVisibleToMcLoader(env))
			{
				ensureRuntimeBridgeInnersInjected(env);
				ensureEspBridgeInjected(env);
				ensureLagBridgeInjected(env);
				return true;
			}

			if (RuntimeBridge_class)
			{
				env->DeleteGlobalRef(RuntimeBridge_class);
				RuntimeBridge_class = nullptr;
			}

			std::vector<unsigned char> classBytes;
			if (!extractJarEntry(data, sizeof(data), "io/github/lefraudeur/RuntimeBridge.class", classBytes))
			{
				Logger::Error("Patcher", "injectRuntimeBridge: RuntimeBridge.class missing from embedded jar");
				return false;
			}

			if (!EntityRenderer_class)
			{
				Logger::Error("Patcher", "injectRuntimeBridge: EntityRenderer_class missing");
				return false;
			}

			jobject mcLoader = getClassLoaderFor(env, EntityRenderer_class);
			if (!mcLoader)
			{
				Logger::Error("Patcher", "injectRuntimeBridge: failed to get Minecraft classloader");
				return false;
			}

			// Strategy 0: Java-side reflection (works on Lunar Genesis / Java 17)
			if (ClassPatcher_class)
			{
				jmethodID injectMid = env->GetStaticMethodID(ClassPatcher_class,
					"injectRuntimeBridgeToLoader", "(Ljava/lang/ClassLoader;[B)Z");
				if (injectMid)
				{
					jbyteArray arr = env->NewByteArray(static_cast<jsize>(classBytes.size()));
					env->SetByteArrayRegion(arr, 0, static_cast<jsize>(classBytes.size()),
						reinterpret_cast<const jbyte*>(classBytes.data()));

					jboolean ok = env->CallStaticBooleanMethod(
						ClassPatcher_class, injectMid, mcLoader, arr);
					env->DeleteLocalRef(arr);

					if (env->ExceptionCheck())
					{
						Logger::Error("Patcher", "injectRuntimeBridgeToLoader threw");
						env->ExceptionDescribe();
						env->ExceptionClear();
					}
					else if (ok)
					{
						jclass local = nullptr;
						if (loadClassOnLoader(env, mcLoader, "io.github.lefraudeur.RuntimeBridge", local))
						{
							return finishRuntimeBridgeInjection(env, mcLoader, local,
								"RuntimeBridge injected on MC classloader (ClassPatcher Java helper)");
						}
					}
				}
			}

			// Strategy 1: MC/Genesis classloader (required for Lunar Ichor — bootstrap is NOT visible)
			jclass local = nullptr;
			if (loadClassOnLoader(env, mcLoader, "io.github.lefraudeur.RuntimeBridge", local))
			{
				return finishRuntimeBridgeInjection(env, mcLoader, local,
					"RuntimeBridge already on MC classloader");
			}

			if (defineClassViaReflection(env, mcLoader, "io.github.lefraudeur.RuntimeBridge", classBytes, local))
			{
				return finishRuntimeBridgeInjection(env, mcLoader, local,
					"RuntimeBridge defined on MC classloader (reflection)");
			}

			// Append bridge jar to MC loader classpath then loadClass (works on some URLClassLoader-based loaders)
			const std::string bridgeJar = writeRuntimeBridgeJarToTemp(classBytes);
			if (!bridgeJar.empty())
			{
				jclass urlClass = env->FindClass("java/net/URL");
				jmethodID urlCtor = urlClass ? env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V") : nullptr;
				const std::string fileUrl = toFileUrl(bridgeJar);
				jstring urlStr = env->NewStringUTF(fileUrl.c_str());
				jobject url = (urlClass && urlCtor) ? env->NewObject(urlClass, urlCtor, urlStr) : nullptr;

				jclass loaderClass = env->GetObjectClass(mcLoader);
				jmethodID addURL = loaderClass ? env->GetMethodID(loaderClass, "addURL", "(Ljava/net/URL;)V") : nullptr;
				if (addURL && url)
					env->CallVoidMethod(mcLoader, addURL, url);
				if (env->ExceptionCheck()) { env->ExceptionClear(); addURL = nullptr; }

				if (addURL && loadClassOnLoader(env, mcLoader, "io.github.lefraudeur.RuntimeBridge", local))
				{
					if (url) env->DeleteLocalRef(url);
					if (urlStr) env->DeleteLocalRef(urlStr);
					if (urlClass) env->DeleteLocalRef(urlClass);
					if (loaderClass) env->DeleteLocalRef(loaderClass);
					return finishRuntimeBridgeInjection(env, mcLoader, local,
						("RuntimeBridge loaded on MC classloader via addURL: " + bridgeJar).c_str());
				}

				if (url) env->DeleteLocalRef(url);
				if (urlStr) env->DeleteLocalRef(urlStr);
				if (urlClass) env->DeleteLocalRef(urlClass);
				if (loaderClass) env->DeleteLocalRef(loaderClass);
			}

			local = env->DefineClass(
				"io/github/lefraudeur/RuntimeBridge",
				mcLoader,
				reinterpret_cast<const jbyte*>(classBytes.data()),
				static_cast<jsize>(classBytes.size()));

			if (local && !env->ExceptionCheck())
			{
				return finishRuntimeBridgeInjection(env, mcLoader, local,
					"RuntimeBridge defined on MC classloader (JNI DefineClass)");
			}

			if (env->ExceptionCheck())
			{
				Logger::Error("Patcher", "JNI DefineClass(RuntimeBridge) on MC loader failed");
				env->ExceptionDescribe();
				env->ExceptionClear();
			}
			env->DeleteLocalRef(mcLoader);

			// Strategy 2: bootstrap fallback — does NOT work for patched MC bytecode on Lunar
			if (!bridgeJar.empty())
			{
				jvmtiError jerr = Java::tiEnv->AddToBootstrapClassLoaderSearch(bridgeJar.c_str());
				if (jerr == JVMTI_ERROR_NONE)
				{
					local = loadRuntimeBridgeViaForName(env);
					if (local)
						env->DeleteLocalRef(local);
					if (env->ExceptionCheck()) env->ExceptionClear();
				}
				Logger::Warn("Patcher", "Bootstrap RuntimeBridge not visible to Genesis — MC injection required");
			}

			Logger::Error("Patcher", "All RuntimeBridge injection strategies failed");
			return false;
		}

		void loadJar(jobject classLoader, const unsigned char* jarBytes, size_t size)
		{
			(void)jarBytes;
			(void)size;
			bootstrapPatcherClasses(Java::Env, classLoader);
		}

		void gc()
		{
			jclass System_class = Java::Env->FindClass("java/lang/System");
			jmethodID gcID = Java::Env->GetStaticMethodID(System_class, "gc", "()V");
			Java::Env->CallStaticVoidMethod(System_class, gcID);
			Java::Env->DeleteLocalRef(System_class);
		}

		jobject newClassLoader(const std::string& jarPath)
		{
			if (jarPath.empty()) return nullptr;

			const std::string fileUrl = toFileUrl(jarPath);
			Logger::Debug("Patcher", "URLClassLoader URL: " + fileUrl);

			jclass urlClass = Java::Env->FindClass("java/net/URL");
			if (!urlClass) return nullptr;
			jmethodID urlContructor = Java::Env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V");
			jstring str = Java::Env->NewStringUTF(fileUrl.c_str());
			jobject url = Java::Env->NewObject(urlClass, urlContructor, str);
			if (Java::Env->ExceptionCheck())
			{
				Java::Env->ExceptionClear();
				Java::Env->DeleteLocalRef(urlClass);
				Java::Env->DeleteLocalRef(str);
				return nullptr;
			}
			jobjectArray urls = Java::Env->NewObjectArray(1, urlClass, url);
			jclass URLClassLoaderClass = Java::Env->FindClass("java/net/URLClassLoader");
			if (!URLClassLoaderClass)
			{
				Java::Env->DeleteLocalRef(urlClass);
				Java::Env->DeleteLocalRef(url);
				Java::Env->DeleteLocalRef(str);
				Java::Env->DeleteLocalRef(urls);
				return nullptr;
			}
			// Parent=null keeps Minecraft's bundled ASM5 off this loader's classpath.
			jmethodID URLClassLoaderContructor = Java::Env->GetMethodID(
				URLClassLoaderClass, "<init>", "([Ljava/net/URL;Ljava/lang/ClassLoader;)V");
			jobject URLClassLoader = Java::Env->NewObject(
				URLClassLoaderClass, URLClassLoaderContructor, urls, nullptr);
			if (Java::Env->ExceptionCheck())
				Java::Env->ExceptionClear();

			Java::Env->DeleteLocalRef(urlClass);
			Java::Env->DeleteLocalRef(url);
			Java::Env->DeleteLocalRef(str);
			Java::Env->DeleteLocalRef(urls);
			Java::Env->DeleteLocalRef(URLClassLoaderClass);

			return URLClassLoader;
		}

		void JNICALL invokeNativeOnRender3D(JNIEnv* env, jclass clazz, jfloat partialTicks)
		{
			if (!Base::IsRunning() || Base::ShuttingDown.load(std::memory_order_acquire))
				return;
			ModuleManager::OnRender3D(partialTicks);
		}

		void JNICALL invokeNativeOnWalkingUpdatePre(JNIEnv* env, jclass clazz)
		{
			if (!Base::IsRunning() || Base::ShuttingDown.load(std::memory_order_acquire))
				return;
			ModuleManager::OnGameTick();
		}

		void JNICALL invokeNativeOnRunTickPre(JNIEnv* env, jclass clazz)
		{
			(void)env;
			(void)clazz;
			if (!Base::IsRunning() || Base::ShuttingDown.load(std::memory_order_acquire))
				return;
			ModuleManager::OnRunTickPre();
		}

		void setClassPatcherTargetLoader(JNIEnv* env)
		{
			if (!ClassPatcher_class || !EntityRenderer_class)
				return;

			jobject mcLoader = getClassLoaderFor(env, EntityRenderer_class);
			if (!mcLoader)
			{
				Logger::Warn("Patcher", "setTargetClassLoader: MC classloader unavailable");
				return;
			}

			jmethodID mid = env->GetStaticMethodID(
				ClassPatcher_class, "setTargetClassLoader", "(Ljava/lang/ClassLoader;)V");
			if (mid)
			{
				env->CallStaticVoidMethod(ClassPatcher_class, mid, mcLoader);
				if (env->ExceptionCheck())
				{
					Logger::Error("Patcher", "setTargetClassLoader threw");
					env->ExceptionDescribe();
					env->ExceptionClear();
				}
				else
				{
					Logger::Debug("Patcher", "ClassPatcher target classloader set (MC loader for COMPUTE_FRAMES)");
				}
			}
			env->DeleteLocalRef(mcLoader);
		}

		void verifyAsmClasspath(JNIEnv* env)
		{
			if (!ClassPatcher_class)
				return;

			jmethodID verifyMid = env->GetStaticMethodID(ClassPatcher_class, "verifyAsmClasspath", "()Z");
			if (!verifyMid)
				return;

			jboolean asmOk = env->CallStaticBooleanMethod(ClassPatcher_class, verifyMid);
			if (env->ExceptionCheck())
			{
				env->ExceptionDescribe();
				env->ExceptionClear();
			}
			else if (!asmOk)
				Logger::Error("Patcher", "verifyAsmClasspath returned false");
		}

		void retransformClasses()
		{
			if (!ClassPatcher_class || !RuntimeBridge_class)
			{
				Logger::Warn("Patcher", "Skip retransform — ClassPatcher or RuntimeBridge not loaded");
				return;
			}

			Java::tiEnv->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
			setClassPatcherTargetLoader(Java::Env);

			struct Target { jclass* cls; const char* name; };
			Target targets[] = {
				{ &EntityRenderer_class, "EntityRenderer (reach/3D/hitbox raytrace)" },
				{ &RendererLivingEntity_class, "RendererLivingEntity (hide vanilla nametags)" },
				{ &RenderPlayer_class, "RenderPlayer (hide vanilla nametags)" },
				{ &Minecraft_class, "Minecraft (autoclicker runTick input mask)" },
				{ &EntityPlayerSP_class, "EntityPlayerSP (silent rotation)" },
			{ &GuiIngame_class, "GuiIngame (ESP 2D/Outline overlay)" },
			{ &NetworkManager_class, "NetworkManager (packet lag / knockback delay)" },
			{ &PlayerControllerMP_class, "PlayerControllerMP (NoInteract block-use skip)" },
		};

			int okCount = 0;
			for (const Target& t : targets)
			{
				if (!t.cls || !*t.cls)
				{
					Logger::Warn("Patcher", std::string("Skip retransform — class ref missing: ") + t.name);
					markRetransform(t.name, false);
					continue;
				}
				jvmtiError err = Java::tiEnv->RetransformClasses(1, t.cls);
				if (err == JVMTI_ERROR_NONE)
				{
					++okCount;
					markRetransform(t.name, true);
					Logger::Info("Patcher", std::string("Retransform OK: ") + t.name);
				}
				else
				{
					markRetransform(t.name, false);
					Logger::Error("Patcher", std::string("Retransform FAILED: ") + t.name
						+ " code=" + std::to_string(err) + " (" + jvmtiErrorName(err) + ")");
				}
			}

			s_retransformOkCount.store(okCount, std::memory_order_release);
			Java::tiEnv->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
		}

		void resolveClassRefs()
		{
			if (!EntityRenderer_class)
			{
				jclass localEntityRenderer = nullptr;
				Java::AssignClass("net.minecraft.client.renderer.EntityRenderer", localEntityRenderer);
				if (localEntityRenderer)
				{
					EntityRenderer_class = (jclass)Java::Env->NewGlobalRef(localEntityRenderer);
					Java::Env->DeleteLocalRef(localEntityRenderer);
				}
			}

			if (!RendererLivingEntity_class)
			{
				jclass localRendererLivingEntity = nullptr;
				Java::AssignClass("net.minecraft.client.renderer.entity.RendererLivingEntity", localRendererLivingEntity);
				if (localRendererLivingEntity)
				{
					RendererLivingEntity_class = (jclass)Java::Env->NewGlobalRef(localRendererLivingEntity);
					Java::Env->DeleteLocalRef(localRendererLivingEntity);
				}
			}

			if (!RenderPlayer_class)
			{
				jclass localRenderPlayer = nullptr;
				Java::AssignClass("net.minecraft.client.renderer.entity.RenderPlayer", localRenderPlayer);
				if (localRenderPlayer)
				{
					RenderPlayer_class = (jclass)Java::Env->NewGlobalRef(localRenderPlayer);
					Java::Env->DeleteLocalRef(localRenderPlayer);
				}
			}

			if (!EntityPlayerSP_class)
			{
				jclass localEntityPlayerSP = nullptr;
				Java::AssignClass("net.minecraft.client.entity.EntityPlayerSP", localEntityPlayerSP);
				if (localEntityPlayerSP)
				{
					EntityPlayerSP_class = (jclass)Java::Env->NewGlobalRef(localEntityPlayerSP);
					Java::Env->DeleteLocalRef(localEntityPlayerSP);
				}
			}

			if (!Minecraft_class)
			{
				jclass localMinecraft = nullptr;
				Java::AssignClass("net.minecraft.client.Minecraft", localMinecraft);
				if (localMinecraft)
				{
					Minecraft_class = (jclass)Java::Env->NewGlobalRef(localMinecraft);
					Java::Env->DeleteLocalRef(localMinecraft);
				}
			}

			if (!GuiIngame_class)
			{
				jclass localGuiIngame = nullptr;
				Java::AssignClass("net.minecraft.client.gui.GuiIngame", localGuiIngame);
				if (localGuiIngame)
				{
					GuiIngame_class = (jclass)Java::Env->NewGlobalRef(localGuiIngame);
					Java::Env->DeleteLocalRef(localGuiIngame);
				}
			}

			if (!NetworkManager_class)
			{
				jclass localNm = nullptr;
				Java::AssignClass("net.minecraft.network.NetworkManager", localNm);
				if (localNm)
				{
					NetworkManager_class = (jclass)Java::Env->NewGlobalRef(localNm);
					Java::Env->DeleteLocalRef(localNm);
				}
			}

			if (!PlayerControllerMP_class)
			{
				jclass localPc = nullptr;
				Java::AssignClass("net.minecraft.client.multiplayer.PlayerControllerMP", localPc);
				if (localPc)
				{
					PlayerControllerMP_class = (jclass)Java::Env->NewGlobalRef(localPc);
					Java::Env->DeleteLocalRef(localPc);
				}
			}
		}

		void registerNativeIfNeeded()
		{
			if (s_nativeRegistered.load(std::memory_order_acquire))
				return;

			if (!resolveClassPatcher(Java::Env))
				return;

			if (!injectRuntimeBridge(Java::Env))
			{
				Logger::Error("Patcher", "RuntimeBridge injection failed — hooks will crash at render time");
				return;
			}

			JNINativeMethod patcherMethods[] = {
				{ (char*)"logFromJava", (char*)"(Ljava/lang/String;Ljava/lang/String;)V", (void*)logFromJava },
			};
			if (Java::Env->RegisterNatives(ClassPatcher_class, patcherMethods, 1) != 0)
			{
				Logger::Error("Patcher", "RegisterNatives failed for ClassPatcher.logFromJava");
				if (Java::Env->ExceptionCheck())
				{
					Java::Env->ExceptionDescribe();
					Java::Env->ExceptionClear();
				}
				return;
			}

			JNINativeMethod bridgeMethods[] = {
				{ (char*)"invokeNativeOnRender3D", (char*)"(F)V", (void*)invokeNativeOnRender3D },
				{ (char*)"invokeNativeOnWalkingUpdatePre", (char*)"()V", (void*)invokeNativeOnWalkingUpdatePre },
				{ (char*)"invokeNativeOnRunTickPre", (char*)"()V", (void*)invokeNativeOnRunTickPre },
				{ (char*)"logFromJava", (char*)"(Ljava/lang/String;Ljava/lang/String;)V", (void*)logFromJava },
			{ (char*)"pushItemLog", (char*)"(Ljava/lang/String;)V", (void*)pushItemLog },
			{ (char*)"itemWhitelistMatch", (char*)"(Ljava/lang/String;IIZLjava/lang/String;)Z", (void*)itemWhitelistMatch },
		};
		if (Java::Env->RegisterNatives(RuntimeBridge_class, bridgeMethods, 6) == 0)
			{
				s_nativeRegistered.store(true, std::memory_order_release);
				Logger::Info("Patcher", "Registered natives on RuntimeBridge + ClassPatcher");

				jmethodID warmupMid = Java::Env->GetStaticMethodID(RuntimeBridge_class, "warmupReflection", "()V");
				if (warmupMid)
				{
					Java::Env->CallStaticVoidMethod(RuntimeBridge_class, warmupMid);
					if (Java::Env->ExceptionCheck())
					{
						Logger::Error("Patcher", "RuntimeBridge.warmupReflection threw");
						Java::Env->ExceptionDescribe();
						Java::Env->ExceptionClear();
					}
				}
			}
			else
			{
				Logger::Error("Patcher", "RegisterNatives failed for RuntimeBridge");
				if (Java::Env->ExceptionCheck())
				{
					Java::Env->ExceptionDescribe();
					Java::Env->ExceptionClear();
				}
			}
		}

		bool ensurePatcherLoaded(JNIEnv* env)
		{
			if (patcherClassLoader && ClassPatcher_class && RuntimeBridge_class
				&& isRuntimeBridgeVisibleToMcLoader(env))
				return true;

			if (!patcherClassLoader)
			{
				const std::string jarPath = writeEmbeddedJarToTemp(data, sizeof(data));
				jobject localClassLoader = jarPath.empty() ? nullptr : newClassLoader(jarPath);
				if (!localClassLoader)
				{
					Logger::Error("Patcher", "Failed to create URLClassLoader for ClassPatcher");
					return false;
				}

				patcherClassLoader = env->NewGlobalRef(localClassLoader);
				env->DeleteLocalRef(localClassLoader);

				loadJar(patcherClassLoader, data, sizeof(data));
				setContextClassLoader(env, patcherClassLoader);
			}

			if (!resolveClassPatcher(env))
			{
				Logger::Error("Patcher", "Failed to resolve ClassPatcher after jar load");
				return false;
			}

			registerNativeIfNeeded();
			if (!RuntimeBridge_class || !isRuntimeBridgeVisibleToMcLoader(env))
			{
				Logger::Error("Patcher", "RuntimeBridge not visible to MC classloader after registerNativeIfNeeded");
				return false;
			}

			verifyAsmClasspath(env);
			setClassPatcherTargetLoader(env);
			return true;
		}

		void JNICALL ClassFileLoadHook
		(
			jvmtiEnv* jvmti_env, 
			JNIEnv* jni_env, 
			jclass class_being_redefined, 
			jobject loader, 
			const char* name, 
			jobject protection_domain, 
			jint class_data_len, 
			const unsigned char* class_data, 
			jint* new_class_data_len, 
			unsigned char** new_class_data
		)
		{
			std::function<void(const char*, const std::string&, const std::string&)> patchClass =
				[=](const char* targetClass, const std::string& patchMethod, const std::string& methodToPatch)
			{
				Logger::Info("Patcher", std::string("Bytecode patch: ") + targetClass + " -> "
					+ patchMethod + "() hooking MC method '" + methodToPatch + "'"
					+ " (" + std::to_string(class_data_len) + " bytes in)");

				if (!resolveClassPatcher(jni_env))
				{
					Logger::Error("Patcher", std::string("ClassPatcher unavailable during hook for ") + targetClass);
					s_patchFailCount.fetch_add(1);
					return;
				}
				jbyteArray original_class_bytes = jni_env->NewByteArray(class_data_len);
				jni_env->SetByteArrayRegion(original_class_bytes, 0, class_data_len, (const jbyte*)class_data);

				jmethodID patchMethodID = jni_env->GetStaticMethodID(ClassPatcher_class, patchMethod.c_str(), "([BLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)[B");
				if (!patchMethodID) {
					Logger::Error("Patcher", "GetStaticMethodID failed: ClassPatcher." + patchMethod);
					s_patchFailCount.fetch_add(1);
					if (jni_env->ExceptionCheck())
					{
						jni_env->ExceptionDescribe();
						jni_env->ExceptionClear();
					}
					jni_env->DeleteLocalRef(original_class_bytes);
					return;
				}

				jstring methodToPatchStr = jni_env->NewStringUTF(methodToPatch.c_str());
				jstring ThreadContextClassName = jni_env->NewStringUTF("org/apache/logging/log4j/ThreadContext");
				jstring emptyMapName = jni_env->NewStringUTF("EMPTY_MAP");

				jbyteArray new_class_bytes = (jbyteArray)jni_env->CallStaticObjectMethod(
					ClassPatcher_class,
					patchMethodID,
					original_class_bytes,
					methodToPatchStr,
					ThreadContextClassName,
					emptyMapName
				);

				if (jni_env->ExceptionCheck()) {
					Logger::Error("Patcher", "Exception in ClassPatcher." + patchMethod + " for " + std::string(targetClass));
					jni_env->ExceptionDescribe();
					jni_env->ExceptionClear();
					s_patchFailCount.fetch_add(1);
					jni_env->DeleteLocalRef(methodToPatchStr);
					jni_env->DeleteLocalRef(ThreadContextClassName);
					jni_env->DeleteLocalRef(emptyMapName);
					jni_env->DeleteLocalRef(original_class_bytes);
					return;
				}

				jni_env->DeleteLocalRef(methodToPatchStr);
				jni_env->DeleteLocalRef(ThreadContextClassName);
				jni_env->DeleteLocalRef(emptyMapName);
				jni_env->DeleteLocalRef(original_class_bytes);

			if (new_class_bytes) {
				jint outLen = jni_env->GetArrayLength(new_class_bytes);
				*new_class_data_len = outLen;
				jvmti_env->Allocate(*new_class_data_len, new_class_data);
				jni_env->GetByteArrayRegion(new_class_bytes, 0, *new_class_data_len, (jbyte*)*new_class_data);
				jni_env->DeleteLocalRef(new_class_bytes);
				s_patchSuccessCount.fetch_add(1);
				markPatched(targetClass);
				dumpClassPairOnce(targetClass, class_data, class_data_len, *new_class_data, *new_class_data_len);
					Logger::Info("Patcher", std::string("Patch applied: ") + targetClass
						+ " " + patchMethod + " output=" + std::to_string(outLen) + " bytes");
				} else {
					Logger::Error("Patcher", "ClassPatcher." + patchMethod + " returned null for " + std::string(targetClass));
					s_patchFailCount.fetch_add(1);
				}
			};

			if (jni_env->IsSameObject(class_being_redefined, EntityRenderer_class))
				patchClass("EntityRenderer", "patchEntityRenderer", "getMouseOver");
			else if (jni_env->IsSameObject(class_being_redefined, RendererLivingEntity_class))
				patchClass("RendererLivingEntity", "patchRendererLivingEntity", "renderLivingLabel");
			else if (jni_env->IsSameObject(class_being_redefined, RenderPlayer_class))
				patchClass("RenderPlayer", "patchRendererLivingEntity", "renderLivingLabel");
			else if (jni_env->IsSameObject(class_being_redefined, EntityPlayerSP_class))
				patchClass("EntityPlayerSP", "patchEntityPlayerSP", "onUpdateWalkingPlayer");
			else if (Minecraft_class && jni_env->IsSameObject(class_being_redefined, Minecraft_class))
				patchClass("Minecraft", "patchMinecraft", "runTick");
			else if (GuiIngame_class && jni_env->IsSameObject(class_being_redefined, GuiIngame_class))
				patchClass("GuiIngame", "patchGuiIngame", "renderGameOverlay");
			else if (NetworkManager_class && jni_env->IsSameObject(class_being_redefined, NetworkManager_class))
				patchClass("NetworkManager", "patchNetworkManager", "channelRead0");
			else if (PlayerControllerMP_class && jni_env->IsSameObject(class_being_redefined, PlayerControllerMP_class))
				patchClass("PlayerControllerMP", "patchPlayerControllerMP", "onPlayerRightClick");
		}
	}
	void Init()
	{
		s_nativeRegistered.store(false, std::memory_order_release);
		s_retransformOkCount.store(0, std::memory_order_release);
		s_patchSuccessCount.store(0, std::memory_order_release);
		s_patchFailCount.store(0, std::memory_order_release);
		s_patchedEntityRenderer.store(false, std::memory_order_release);
		s_patchedMinecraft.store(false, std::memory_order_release);
		s_patchedEntityPlayerSP.store(false, std::memory_order_release);
		s_patchedNetworkManager.store(false, std::memory_order_release);
		s_patchedPlayerControllerMP.store(false, std::memory_order_release);
		s_patchedGuiIngame.store(false, std::memory_order_release);
		s_patchedRendererLivingEntity.store(false, std::memory_order_release);
		s_patchedRenderPlayer.store(false, std::memory_order_release);
		s_rtEntityRenderer.store(false, std::memory_order_release);
		s_rtMinecraft.store(false, std::memory_order_release);
		s_rtEntityPlayerSP.store(false, std::memory_order_release);
		s_rtNetworkManager.store(false, std::memory_order_release);
		s_rtPlayerControllerMP.store(false, std::memory_order_release);
		s_rtGuiIngame.store(false, std::memory_order_release);
		s_rtRendererLivingEntity.store(false, std::memory_order_release);
		s_rtRenderPlayer.store(false, std::memory_order_release);
		s_lastRunTickPreMs.store(0, std::memory_order_release);

		jvmtiCapabilities capabilities{};
		capabilities.can_retransform_classes = JVMTI_ENABLE;
		Java::tiEnv->AddCapabilities(&capabilities);
		jvmtiEventCallbacks callbacks{};
		callbacks.ClassFileLoadHook = &ClassFileLoadHook;
		Java::tiEnv->SetEventCallbacks(&callbacks, sizeof(jvmtiEventCallbacks));
		// Hook is enabled only inside retransformClasses() — not during jar bootstrap.

		//here I am using an empty map, already in the game, to hide and store my cheat data, the getMouseOver than accesses this map (see asm folder)
		jclass localEntityRenderer = nullptr;
		Java::AssignClass("net.minecraft.client.renderer.EntityRenderer", localEntityRenderer);
		if (localEntityRenderer)
		{
			EntityRenderer_class = (jclass)Java::Env->NewGlobalRef(localEntityRenderer);
			Java::Env->DeleteLocalRef(localEntityRenderer);
		}

		jclass localRendererLivingEntity = nullptr;
		Java::AssignClass("net.minecraft.client.renderer.entity.RendererLivingEntity", localRendererLivingEntity);
		if (localRendererLivingEntity)
		{
			RendererLivingEntity_class = (jclass)Java::Env->NewGlobalRef(localRendererLivingEntity);
			Java::Env->DeleteLocalRef(localRendererLivingEntity);
		}

		jclass localRenderPlayer = nullptr;
		Java::AssignClass("net.minecraft.client.renderer.entity.RenderPlayer", localRenderPlayer);
		if (localRenderPlayer)
		{
			RenderPlayer_class = (jclass)Java::Env->NewGlobalRef(localRenderPlayer);
			Java::Env->DeleteLocalRef(localRenderPlayer);
		}

		jclass localEntityPlayerSP = nullptr;
		Java::AssignClass("net.minecraft.client.entity.EntityPlayerSP", localEntityPlayerSP);
		if (localEntityPlayerSP)
		{
			EntityPlayerSP_class = (jclass)Java::Env->NewGlobalRef(localEntityPlayerSP);
			Java::Env->DeleteLocalRef(localEntityPlayerSP);
		}

		jclass localMinecraft = nullptr;
		Java::AssignClass("net.minecraft.client.Minecraft", localMinecraft);
		if (localMinecraft)
		{
			Minecraft_class = (jclass)Java::Env->NewGlobalRef(localMinecraft);
			Java::Env->DeleteLocalRef(localMinecraft);
		}

		jclass localGuiIngame = nullptr;
		Java::AssignClass("net.minecraft.client.gui.GuiIngame", localGuiIngame);
		if (localGuiIngame)
		{
			GuiIngame_class = (jclass)Java::Env->NewGlobalRef(localGuiIngame);
			Java::Env->DeleteLocalRef(localGuiIngame);
		}

		jclass localNm = nullptr;
		Java::AssignClass("net.minecraft.network.NetworkManager", localNm);
		if (localNm)
		{
			NetworkManager_class = (jclass)Java::Env->NewGlobalRef(localNm);
			Java::Env->DeleteLocalRef(localNm);
		}

		jclass localPc = nullptr;
		Java::AssignClass("net.minecraft.client.multiplayer.PlayerControllerMP", localPc);
		if (localPc)
		{
			PlayerControllerMP_class = (jclass)Java::Env->NewGlobalRef(localPc);
			Java::Env->DeleteLocalRef(localPc);
		}

		jclass ThreadContext_class = Java::findClass(Java::Env, Java::tiEnv, "org/apache/logging/log4j/ThreadContext");
		jfieldID EMPTY_MAP_ID = Java::Env->GetStaticFieldID(ThreadContext_class, "EMPTY_MAP", "Ljava/util/Map;");
		
		jobject localOriginalEmptyMap = Java::Env->GetStaticObjectField(ThreadContext_class, EMPTY_MAP_ID);
		if (localOriginalEmptyMap)
		{
			original_EMPTY_MAP = Java::Env->NewGlobalRef(localOriginalEmptyMap);
			Java::Env->DeleteLocalRef(localOriginalEmptyMap);
		}
		

		jclass hashmap_class = Java::Env->FindClass("java/util/HashMap");
		jmethodID constructor = Java::Env->GetMethodID(hashmap_class, "<init>", "()V");
		
		jobject localEmptyMap = Java::Env->NewObject(hashmap_class, constructor);
		if (localEmptyMap)
		{
			EMPTY_MAP = Java::Env->NewGlobalRef(localEmptyMap);
			Java::Env->DeleteLocalRef(localEmptyMap);
		}

		Java::Env->SetStaticObjectField(ThreadContext_class, EMPTY_MAP_ID, EMPTY_MAP);
		Java::Env->DeleteLocalRef(hashmap_class);
		Java::Env->DeleteLocalRef(ThreadContext_class);

		// Cache Map.put()/get() method IDs for use in put()/get() calls
		jclass map_class = Java::Env->FindClass("java/util/Map");
		if (map_class)
		{
			mapPutMethodID = Java::Env->GetMethodID(map_class, "put",
				"(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
			mapGetMethodID = Java::Env->GetMethodID(map_class, "get",
				"(Ljava/lang/Object;)Ljava/lang/Object;");
			Java::Env->DeleteLocalRef(map_class);
		}

		put("reach_distance", "3.0");
		put("nametags_enabled", "false");
		put("esp_enabled", "false");
		put("nonametag_enabled", "false");
		put("antidebuff_blindness", "false");
		put("antidebuff_nausea", "false");
		put("autoblock_force_anim", "false");
		put("backtrack_enabled", "false");
		put("backtrack_max_delay", "200");
		put("backtrack_disable_on_hit", "true");

		if (ensurePatcherLoaded(Java::Env))
		{
			retransformClasses();
			logStatus("Init complete");
		}
		else
		{
			Logger::Error("Patcher", "Init bootstrap failed — will retry from Tick()");
			logStatus("Init incomplete");
		}

		Java::tiEnv->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);

		gc();
		s_lastRetryTime = std::chrono::steady_clock::now();
	}

	bool IsReady()
	{
		if (!s_nativeRegistered.load(std::memory_order_acquire) || !RuntimeBridge_class)
			return false;

		// Lunar often rejects Minecraft / EntityPlayerSP retransform (INVALID_CLASS_FORMAT).
		// EntityRenderer.getMouseOver HEAD now drives OnRunTickPre as a fallback, so those
		// two classes are optional. NetworkManager remains required for lag modules.
		const bool critical =
			s_rtEntityRenderer.load(std::memory_order_acquire)
			&& s_rtNetworkManager.load(std::memory_order_acquire);
		if (!critical)
			return false;

		return isRuntimeBridgeVisibleToMcLoader(Java::Env);
	}

	void NoteRunTickPre()
	{
		const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		s_lastRunTickPreMs.store(ms, std::memory_order_release);
	}

	long long LastRunTickPreMs()
	{
		return s_lastRunTickPreMs.load(std::memory_order_acquire);
	}

	bool IsRunTickAlive(long long maxAgeMs)
	{
		const long long last = s_lastRunTickPreMs.load(std::memory_order_acquire);
		if (last <= 0) return false;
		const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		return (now - last) <= maxAgeMs;
	}

	bool IsClassPatched(const char* shortName)
	{
		if (!shortName) return false;
		if (strcmp(shortName, "Minecraft") == 0) return s_patchedMinecraft.load();
		if (strcmp(shortName, "EntityRenderer") == 0) return s_patchedEntityRenderer.load();
		if (strcmp(shortName, "EntityPlayerSP") == 0) return s_patchedEntityPlayerSP.load();
		if (strcmp(shortName, "NetworkManager") == 0) return s_patchedNetworkManager.load();
		if (strcmp(shortName, "PlayerControllerMP") == 0) return s_patchedPlayerControllerMP.load();
		if (strcmp(shortName, "GuiIngame") == 0) return s_patchedGuiIngame.load();
		if (strcmp(shortName, "RendererLivingEntity") == 0) return s_patchedRendererLivingEntity.load();
		if (strcmp(shortName, "RenderPlayer") == 0) return s_patchedRenderPlayer.load();
		return false;
	}

	void Tick()
	{
		if (IsReady() || Base::ShuttingDown.load(std::memory_order_acquire))
		{
			// Periodic heartbeat warning even when "ready" — Minecraft patch can regress.
			static long long s_lastHbWarn = 0;
			const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			if (IsReady() && !IsRunTickAlive(2000) && now - s_lastHbWarn > 5000)
			{
				s_lastHbWarn = now;
				Logger::Warn("Patcher", "OnRunTickPre heartbeat missing — Aim/AC drain will stall");
			}
			if (IsReady())
				return;
		}

		const auto now = std::chrono::steady_clock::now();

		// Exponential backoff: 5s -> 10s -> 20s -> 40s -> 60s cap. On Lunar some
		// retransforms are rejected permanently; spamming RetransformClasses every
		// 5s floods the log and re-runs the mixin chain for no benefit.
		static int s_retryAttempt = 0;
		static const int kBackoffSec[] = { 5, 10, 20, 40, 60 };
		const int stage = s_retryAttempt < 5 ? s_retryAttempt : 4;
		if (now - s_lastRetryTime < std::chrono::seconds(kBackoffSec[stage]))
			return;
		s_lastRetryTime = now;
		++s_retryAttempt;

		Logger::Warn("Patcher", "Not ready — retrying patcher bootstrap + retransform (attempt "
			+ std::to_string(s_retryAttempt) + ")");
		logStatus("Before retry");

		resolveClassRefs();
		if (!ensurePatcherLoaded(Java::Env))
		{
			logStatus("Bootstrap failed");
			return;
		}

		retransformClasses();

		logStatus("After retry");

		if (s_retryAttempt == 8)
			Logger::Warn("Patcher", "Retransform still rejected after 8 attempts — overlay tick fallback stays active; retrying every 60s");
	}
	void Kill()
	{
		retransformClasses();

		jclass ThreadContext_class = Java::findClass(Java::Env, Java::tiEnv, "org/apache/logging/log4j/ThreadContext");
		jfieldID EMPTY_MAP_ID = Java::Env->GetStaticFieldID(ThreadContext_class, "EMPTY_MAP", "Ljava/util/Map;");
		Java::Env->SetStaticObjectField(ThreadContext_class, EMPTY_MAP_ID, original_EMPTY_MAP);

		Java::Env->DeleteLocalRef(ThreadContext_class);
		
		if (original_EMPTY_MAP)
		{
			Java::Env->DeleteGlobalRef(original_EMPTY_MAP);
			original_EMPTY_MAP = nullptr;
		}

		if (EMPTY_MAP)
		{
			Java::Env->DeleteGlobalRef(EMPTY_MAP);
			EMPTY_MAP = nullptr;
		}

		mapPutMethodID = nullptr;
		mapGetMethodID = nullptr;

		if (EntityRenderer_class)
		{
			Java::Env->DeleteGlobalRef(EntityRenderer_class);
			EntityRenderer_class = nullptr;
		}

		if (RendererLivingEntity_class)
		{
			Java::Env->DeleteGlobalRef(RendererLivingEntity_class);
			RendererLivingEntity_class = nullptr;
		}

		if (RenderPlayer_class)
		{
			Java::Env->DeleteGlobalRef(RenderPlayer_class);
			RenderPlayer_class = nullptr;
		}

		if (EntityPlayerSP_class)
		{
			Java::Env->DeleteGlobalRef(EntityPlayerSP_class);
			EntityPlayerSP_class = nullptr;
		}
		if (Minecraft_class)
		{
			Java::Env->DeleteGlobalRef(Minecraft_class);
			Minecraft_class = nullptr;
		}
		if (GuiIngame_class)
		{
			Java::Env->DeleteGlobalRef(GuiIngame_class);
			GuiIngame_class = nullptr;
		}
		if (NetworkManager_class)
		{
			Java::Env->DeleteGlobalRef(NetworkManager_class);
			NetworkManager_class = nullptr;
		}
		if (PlayerControllerMP_class)
		{
			Java::Env->DeleteGlobalRef(PlayerControllerMP_class);
			PlayerControllerMP_class = nullptr;
		}

		if (patcherClassLoader)
		{
			Java::Env->DeleteGlobalRef(patcherClassLoader);
			patcherClassLoader = nullptr;
		}

		if (ClassPatcher_class)
		{
			Java::Env->DeleteGlobalRef(ClassPatcher_class);
			ClassPatcher_class = nullptr;
		}

		if (RuntimeBridge_class)
		{
			Java::Env->DeleteGlobalRef(RuntimeBridge_class);
			RuntimeBridge_class = nullptr;
		}
	}
	namespace
	{
		void putImpl(const std::string& key, const std::string& value, bool force)
		{
			if (!EMPTY_MAP || !mapPutMethodID) return;

			// Katman 1: short-circuit when the value hasn't changed. Each
			// Patcher::put involves 2 NewStringUTF allocations, a Map.put
			// JNI call and 3 local-ref churns. For 16+ active modules at
			// 200 ticks/s, that's 18,000 JNI ops/s of which the vast majority
			// re-push the same static value. We only forward to Java when the
			// string actually changed. `force` skips this when Java may have
			// written the key externally (the cache would lie in that case).
			{
				static std::mutex s_mtx;
				std::lock_guard<std::mutex> _l(s_mtx);
				auto it = s_lastPut.find(key);
				if (!force && it != s_lastPut.end() && it->second == value)
					return;
				s_lastPut[key] = value;
			}

			JNIEnv* env = Java::GetEnv();
			if (!env) return;

			jstring k = env->NewStringUTF(key.c_str());
			jstring v = env->NewStringUTF(value.c_str());
			jobject rs = env->CallObjectMethod(EMPTY_MAP, mapPutMethodID, k, v);

			env->DeleteLocalRef(k);
			env->DeleteLocalRef(v);
			if (rs) env->DeleteLocalRef(rs);

			if (key.rfind("nametags_", 0) == 0 || key == "nonametag_enabled"
				|| key == "reach_distance" || key.rfind("silent_", 0) == 0)
			{
				Logger::Debug("ThreadContext", key + " = " + value);
			}
		}
	}

	void put(const std::string& key, const std::string& value)
	{
		putImpl(key, value, false);
	}

	void putForce(const std::string& key, const std::string& value)
	{
		putImpl(key, value, true);
	}

	std::string get(const std::string& key)
	{
		if (!EMPTY_MAP || !mapGetMethodID) return "";

		JNIEnv* env = Java::GetEnv();
		if (!env) return "";

		static jclass stringClass = nullptr;
		if (!stringClass)
		{
			jclass local = env->FindClass("java/lang/String");
			if (!local) { env->ExceptionClear(); return ""; }
			stringClass = (jclass)env->NewGlobalRef(local);
			env->DeleteLocalRef(local);
		}

		jstring k = env->NewStringUTF(key.c_str());
		jobject rs = env->CallObjectMethod(EMPTY_MAP, mapGetMethodID, k);
		env->DeleteLocalRef(k);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return ""; }
		if (!rs) return "";

		std::string out;
		if (env->IsInstanceOf(rs, stringClass))
		{
			const char* utf = env->GetStringUTFChars((jstring)rs, nullptr);
			if (utf)
			{
				out = utf;
				env->ReleaseStringUTFChars((jstring)rs, utf);
			}
		}
		env->DeleteLocalRef(rs);
		return out;
	}
}
