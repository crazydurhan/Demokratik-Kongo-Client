#pragma once

#include "../../../../ext/jni/jni.h"
#include "../../../../ext/jni/jvmti.h"

#include <functional>
#include <string>

// One-shot JVMTI loaded-class cache. Find() returns a global ref owned by
// the index — callers must NewGlobalRef if they need an independently owned copy.
struct ClassIndex
{
	static bool Build(JNIEnv* env, jvmtiEnv* ti);
	static void Shutdown(JNIEnv* env);

	static bool Ready();
	static size_t Size();

	// slash-form path, e.g. "net/minecraft/client/Minecraft" or "ave"
	static jclass Find(const char* slashPath);
	static void Add(const char* slashPath, jclass globalCls);

	static std::string SignatureOf(JNIEnv* env, jvmtiEnv* ti, jclass cls);
	static void ForEach(const std::function<void(const std::string& slashPath, jclass cls)>& fn);
};
