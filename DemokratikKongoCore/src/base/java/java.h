#pragma once

#include <windows.h>
#include <string>
#include <set>
#include <mutex>

#include "../../../ext/jni/jni.h"
#include "../../../ext/jni/jvmti.h"

struct JNIEnv_ThreadSafe
{
	JNIEnv* Get() const;
	operator JNIEnv*() const { return Get(); }
	JNIEnv* operator->() const { return Get(); }
};

struct Java
{
	static void Init();
	static void Kill();

	static bool AssignClass(std::string name, jclass &out);
	static jclass findClass(JNIEnv* p_env, jvmtiEnv* p_tienv, const std::string& path);
	static bool BindLoaderFromClass(jclass cls);

	static JNIEnv* GetEnv();
	static inline JNIEnv_ThreadSafe Env;
	static inline jvmtiEnv* tiEnv;
	static inline bool Initialized;

private:
	// Threads attached by GetEnv() for proper cleanup (attach is per-THREAD:
	// each attached thread must detach itself; Kill only detaches the caller).
	static inline std::mutex attachedThreadsMutex;
	static inline std::set<DWORD> attachedThreads;
};

