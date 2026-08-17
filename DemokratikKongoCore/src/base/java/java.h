#pragma once

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
	// Track threads attached by GetEnv() for proper cleanup
	static inline std::mutex attachedThreadsMutex;
	static inline std::set<JavaVM*> attachedThreads;
};

