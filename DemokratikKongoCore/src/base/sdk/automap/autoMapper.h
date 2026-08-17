#pragma once

#include "../../../../ext/jni/jni.h"

struct AutoMapper
{
	static void Init();
	static void Shutdown();
	static bool IsReady();

	// ownerPath is internal form, e.g. "net/minecraft/entity/Entity".
	// Returned jclass is a global ref owned by the mapper — NewGlobalRef if you
	// need an independently owned copy.
	static jclass Class(const char* ownerPath);
	static jfieldID Field(const char* ownerPath, const char* mcpName, const char* sig = nullptr);
	static jmethodID Method(const char* ownerPath, const char* mcpName, const char* sig = nullptr);
	static const char* RuntimeName(const char* ownerPath);
};
