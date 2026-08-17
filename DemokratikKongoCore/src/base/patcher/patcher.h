#pragma once
#include "../java/java.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace Patcher
{
	namespace
	{
		inline jobject original_EMPTY_MAP = nullptr;
		inline jobject EMPTY_MAP = nullptr;
		inline jclass Minecraft_class = nullptr;
		inline jclass EntityRenderer_class = nullptr;
		inline jclass RendererLivingEntity_class = nullptr;
		inline jclass RenderPlayer_class = nullptr;
		inline jclass EntityPlayerSP_class = nullptr;
		inline jclass GuiIngame_class = nullptr;
		inline jclass NetworkManager_class = nullptr;
		inline jclass PlayerControllerMP_class = nullptr;
		inline jobject patcherClassLoader = nullptr;

		// Cached JNI method ID for Map.put() - avoids expensive FindClass/GetMethodID per call
		inline jmethodID mapPutMethodID = nullptr;
		inline jmethodID mapGetMethodID = nullptr;

		// Katman 1 (stability plan): thread-safe value cache. We compare the new
		// value against the last one we sent to Java and skip the JNI round-trip
		// (string alloc + Map.put + local ref churn) when nothing has changed.
		// 16+ modules active push ~90 puts per tick (200 ticks/s = 18,000 puts/s)
		// through this function; with static settings that drops to ~0 after warmup.
		inline std::unordered_map<std::string, std::string> s_lastPut;

		void loadJar(jobject classLoader, const unsigned char* jarBytes, size_t size);
		void gc();
		jobject newClassLoader();
		void retransformClasses();
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
		);
	}
	void Init();
	void Kill();
	void Tick();
	bool IsReady();
	void put(const std::string& key, const std::string& value);
	// Bypasses the s_lastPut short-circuit — required when Java may have
	// overwritten the value externally (e.g. damage-disable markers).
	void putForce(const std::string& key, const std::string& value);
	// Reads a flag back from the ThreadContext map ("" when absent/unavailable).
	std::string get(const std::string& key);

	// Client-thread heartbeat: ModuleManager::OnRunTickPre stamps this.
	void NoteRunTickPre();
	long long LastRunTickPreMs();
	bool IsRunTickAlive(long long maxAgeMs = 2000);

	// Per-class bytecode patch status (critical combat set).
	bool IsClassPatched(const char* shortName);
}