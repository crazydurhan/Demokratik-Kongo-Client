#pragma once

#include "../../../ext/jni/jni.h"

// Generic runtime registry over the generated Vape mapping table
// (src/base/sdk/gen/vapeMappingData.h) joined with MCP stable_22 SRG data.
//
// Resolution strategy per member:
//   1. try every candidate name (MCP primary, then SRG / Notch aliases)
//      against the JNI signature (with the correct static/instance accessor)
//   2. vanilla environments: class names inside the signature are rewritten
//      to their Notch counterparts before lookup
//   3. entries without a signature fall back to reflection
//      (FromReflectedField / FromReflectedMethod) matched by candidate names
//   4. methods with a signature but no matching name fall back to
//      JniResolve::MethodByDescriptor (survives renamed members)
//
// All lookups are cached (including failures) and safe to call from any
// thread. Init() also feeds every extracted SRG/Notch alias into
// MappingLoader so StrayCache benefits from the same data.
struct MappedRegistry
{
	static void Init();
	static void Shutdown();

	// ownerPath is internal form, e.g. "net/minecraft/entity/Entity".
	// Returned jclass is a global ref owned by the registry - do NOT delete it.
	static jclass Class(const char* ownerPath);

	// sigFilter (optional, internal-form JNI sig) picks a specific overload.
	static jfieldID Field(const char* ownerPath, const char* mcpName, const char* sigFilter = nullptr);
	static jmethodID Method(const char* ownerPath, const char* mcpName, const char* sigFilter = nullptr);
	static jmethodID Ctor(const char* ownerPath, const char* sigFilter = nullptr);
};
