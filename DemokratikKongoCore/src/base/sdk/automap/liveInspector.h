#pragma once

#include "../../../../ext/jni/jni.h"
#include "shapeTable.h"

#include <string>
#include <vector>

struct InspectedField
{
	std::string name;
	std::string typeSlash;
	char prim = 'L';
	bool isStatic = false;
	jfieldID id = nullptr;
	jclass typeClass = nullptr; // local — valid only during inspect call
};

namespace LiveInspector
{
	jclass FindMinecraftFromClientThread(JNIEnv* env);
	bool BindClassLoaderFrom(JNIEnv* env, jclass cls);

	ClassShape Measure(JNIEnv* env, jclass cls);
	std::vector<InspectedField> ListFields(JNIEnv* env, jclass cls, bool declaredOnly = true);
	int SuperDepth(JNIEnv* env, jclass cls);
	std::string ClassSlashName(JNIEnv* env, jclass cls);
	std::string FieldRuntimeName(JNIEnv* env, jclass cls, jfieldID id);
	std::string MethodRuntimeName(JNIEnv* env, jclass cls, jmethodID id, const char* sig);

	jobject TryGetMinecraftInstance(JNIEnv* env, jclass mcClass, jmethodID getMinecraft);
	void DisambiguateFromInstance(JNIEnv* env, jclass mcClass, jobject mc);
}
