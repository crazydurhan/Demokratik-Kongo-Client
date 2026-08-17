#pragma once

#include "../../ext/jni/jni.h"

#include "mappingLoader.h"
#include "automap/signatureRemap.h"

#include <algorithm>
#include <string>
#include <vector>

namespace JniResolve
{
	inline void ClearException(JNIEnv* env)
	{
		if (env && env->ExceptionCheck())
			env->ExceptionClear();
	}

	// RAII JNI local-reference frame. The local ref table holds only a few
	// hundred entries, and hooks that run once per frame accumulate locals from
	// every module they call - a single missed DeleteLocalRef eventually aborts
	// the JVM with "local reference table overflow". Pushing a frame releases
	// everything created inside it on scope exit.
	//
	// NOTE: no jobject created inside the frame may outlive it. Promote anything
	// that must escape with NewGlobalRef before the frame closes.
	struct LocalFrame
	{
		JNIEnv* env = nullptr;

		explicit LocalFrame(JNIEnv* e, jint capacity = 64)
		{
			if (!e)
				return;
			if (e->PushLocalFrame(capacity) == JNI_OK)
				env = e;
			else
				ClearException(e);
		}

		~LocalFrame()
		{
			if (env)
				env->PopLocalFrame(nullptr);
		}

		LocalFrame(const LocalFrame&) = delete;
		LocalFrame& operator=(const LocalFrame&) = delete;
	};

	inline std::string RemapSig(const char* sig)
	{
		std::string mapped = SignatureRemap::Apply(sig);
		return mapped.empty() && sig ? std::string(sig) : mapped;
	}

	inline jfieldID FieldByDescriptor(JNIEnv* env, jclass cls, const char* sig, bool isStatic);

	inline jfieldID Field(JNIEnv* env, jclass cls, const char* sig, const char* mcpName)
	{
		if (!env || !cls || !sig || !mcpName)
			return nullptr;
		const std::string use = RemapSig(sig);
		for (const std::string& name : MappingLoader::Names(mcpName))
		{
			jfieldID id = env->GetFieldID(cls, name.c_str(), use.c_str());
			if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }
			if (id) return id;
		}
		return FieldByDescriptor(env, cls, use.c_str(), false);
	}

	inline jmethodID Method(JNIEnv* env, jclass cls, const char* sig, const char* mcpName)
	{
		if (!env || !cls || !sig || !mcpName)
			return nullptr;
		const std::string use = RemapSig(sig);
		for (const std::string& name : MappingLoader::Names(mcpName))
		{
			jmethodID id = env->GetMethodID(cls, name.c_str(), use.c_str());
			if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }
			if (id) return id;
		}
		return nullptr;
	}

	inline jmethodID StaticMethod(JNIEnv* env, jclass cls, const char* sig, const char* mcpName)
	{
		if (!env || !cls || !sig || !mcpName)
			return nullptr;
		const std::string use = RemapSig(sig);
		for (const std::string& name : MappingLoader::Names(mcpName))
		{
			jmethodID id = env->GetStaticMethodID(cls, name.c_str(), use.c_str());
			if (env->ExceptionCheck()) { env->ExceptionClear(); continue; }
			if (id) return id;
		}
		return nullptr;
	}

	// Builds a JNI type descriptor from a java.lang.Class via Class.getName()
	// ("int" -> "I", "java.lang.String" -> "Ljava/lang/String;", arrays pass through).
	inline std::string ClassToDescriptor(JNIEnv* env, jclass cls)
	{
		if (!env || !cls)
			return "";

		jclass classClass = env->FindClass("java/lang/Class");
		if (!classClass) { ClearException(env); return ""; }
		jmethodID getName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
		env->DeleteLocalRef(classClass);
		if (!getName) { ClearException(env); return ""; }

		jstring jname = (jstring)env->CallObjectMethod(cls, getName);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return ""; }
		if (!jname)
			return "";

		const char* utf = env->GetStringUTFChars(jname, nullptr);
		std::string name = utf ? utf : "";
		if (utf) env->ReleaseStringUTFChars(jname, utf);
		env->DeleteLocalRef(jname);

		if (name.empty())
			return "";
		if (name[0] == '[')
		{
			std::replace(name.begin(), name.end(), '.', '/');
			return name;
		}
		if (name == "boolean") return "Z";
		if (name == "byte")    return "B";
		if (name == "char")    return "C";
		if (name == "short")   return "S";
		if (name == "int")     return "I";
		if (name == "long")    return "J";
		if (name == "float")   return "F";
		if (name == "double")  return "D";
		if (name == "void")    return "V";
		std::replace(name.begin(), name.end(), '.', '/');
		return "L" + name + ";";
	}

	// Name-independent method resolution: enumerates declared methods via reflection,
	// rebuilds each method's JNI descriptor from its parameter/return types, and matches
	// on sig. Survives obfuscators that rename methods (e.g. Lunar's windowClick).
	// Walks the superclass chain as a last resort.
	inline jmethodID MethodByDescriptor(JNIEnv* env, jclass cls, const char* sig)
	{
		if (!env || !cls || !sig)
			return nullptr;
		const std::string want = RemapSig(sig);
		sig = want.c_str();

		jclass classClass = env->FindClass("java/lang/Class");
		if (!classClass) { ClearException(env); return nullptr; }
		jmethodID getDeclaredMethods = env->GetMethodID(classClass, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
		env->DeleteLocalRef(classClass);
		if (!getDeclaredMethods) { ClearException(env); return nullptr; }

		jclass methodClass = env->FindClass("java/lang/reflect/Method");
		if (!methodClass) { ClearException(env); return nullptr; }
		jmethodID getName = env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;");
		jmethodID getReturnType = env->GetMethodID(methodClass, "getReturnType", "()Ljava/lang/Class;");
		jmethodID getParameterTypes = env->GetMethodID(methodClass, "getParameterTypes", "()[Ljava/lang/Class;");
		if (!getName || !getReturnType || !getParameterTypes)
		{
			ClearException(env);
			env->DeleteLocalRef(methodClass);
			return nullptr;
		}

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
					if (!m) { ClearException(env); continue; }

					std::string desc = "(";
					jobjectArray params = (jobjectArray)env->CallObjectMethod(m, getParameterTypes);
					if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(m); continue; }
					if (params)
					{
						const jsize pc = env->GetArrayLength(params);
						for (jsize j = 0; j < pc; ++j)
						{
							jclass pt = (jclass)env->GetObjectArrayElement(params, j);
							if (pt)
							{
								desc += ClassToDescriptor(env, pt);
								env->DeleteLocalRef(pt);
							}
						}
						env->DeleteLocalRef(params);
					}
					desc += ")";

					jclass rt = (jclass)env->CallObjectMethod(m, getReturnType);
					if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(m); continue; }
					if (rt)
					{
						desc += ClassToDescriptor(env, rt);
						env->DeleteLocalRef(rt);
					}

					if (desc == sig)
					{
						jstring jname = (jstring)env->CallObjectMethod(m, getName);
						if (jname)
						{
							const char* utf = env->GetStringUTFChars(jname, nullptr);
							if (utf)
							{
								jmethodID id = env->GetMethodID(cls, utf, sig);
								if (env->ExceptionCheck()) { env->ExceptionClear(); id = nullptr; }
								if (!id)
								{
									id = env->GetStaticMethodID(cls, utf, sig);
									if (env->ExceptionCheck()) { env->ExceptionClear(); id = nullptr; }
								}
								if (id) result = id;
								env->ReleaseStringUTFChars(jname, utf);
							}
							env->DeleteLocalRef(jname);
						}
					}
					env->DeleteLocalRef(m);
				}
				env->DeleteLocalRef(methods);
			}

			if (!result)
			{
				jclass parent = env->GetSuperclass(current);
				if (env->ExceptionCheck()) { env->ExceptionClear(); parent = nullptr; }
				if (current != cls) env->DeleteLocalRef(current);
				current = parent;
			}
		}

		if (current && current != cls)
			env->DeleteLocalRef(current);
		env->DeleteLocalRef(methodClass);
		return result;
	}

	// Unique-type field resolution. Returns null when more than one field matches.
	inline jfieldID FieldByDescriptor(JNIEnv* env, jclass cls, const char* sig, bool isStatic)
	{
		if (!env || !cls || !sig)
			return nullptr;
		const std::string want = RemapSig(sig);

		jclass classClass = env->FindClass("java/lang/Class");
		if (!classClass) { ClearException(env); return nullptr; }
		jmethodID getDeclaredFields = env->GetMethodID(classClass, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");
		env->DeleteLocalRef(classClass);
		if (!getDeclaredFields) { ClearException(env); return nullptr; }

		jclass fieldClass = env->FindClass("java/lang/reflect/Field");
		if (!fieldClass) { ClearException(env); return nullptr; }
		jmethodID getName = env->GetMethodID(fieldClass, "getName", "()Ljava/lang/String;");
		jmethodID getType = env->GetMethodID(fieldClass, "getType", "()Ljava/lang/Class;");
		jmethodID getMods = env->GetMethodID(fieldClass, "getModifiers", "()I");
		jclass modClass = env->FindClass("java/lang/reflect/Modifier");
		jmethodID isStaticMid = modClass ? env->GetStaticMethodID(modClass, "isStatic", "(I)Z") : nullptr;
		if (!getName || !getType || !getMods || !isStaticMid)
		{
			ClearException(env);
			env->DeleteLocalRef(fieldClass);
			if (modClass) env->DeleteLocalRef(modClass);
			return nullptr;
		}

		jfieldID result = nullptr;
		int matches = 0;
		jclass current = cls;
		while (current && matches < 2)
		{
			jobjectArray fields = (jobjectArray)env->CallObjectMethod(current, getDeclaredFields);
			if (env->ExceptionCheck()) { env->ExceptionClear(); fields = nullptr; }
			if (fields)
			{
				const jsize count = env->GetArrayLength(fields);
				for (jsize i = 0; i < count && matches < 2; ++i)
				{
					jobject f = env->GetObjectArrayElement(fields, i);
					if (!f) { ClearException(env); continue; }
					jint mods = env->CallIntMethod(f, getMods);
					const bool fieldStatic = env->CallStaticBooleanMethod(modClass, isStaticMid, mods) == JNI_TRUE;
					if (fieldStatic != isStatic)
					{
						env->DeleteLocalRef(f);
						continue;
					}
					jclass t = (jclass)env->CallObjectMethod(f, getType);
					if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(f); continue; }
					const std::string desc = t ? ClassToDescriptor(env, t) : "";
					if (t) env->DeleteLocalRef(t);
					if (desc == want)
					{
						++matches;
						result = env->FromReflectedField(f);
					}
					env->DeleteLocalRef(f);
				}
				env->DeleteLocalRef(fields);
			}
			if (matches == 0)
			{
				jclass parent = env->GetSuperclass(current);
				ClearException(env);
				if (current != cls) env->DeleteLocalRef(current);
				current = parent;
			}
			else
				break;
		}
		if (current && current != cls)
			env->DeleteLocalRef(current);
		env->DeleteLocalRef(fieldClass);
		if (modClass) env->DeleteLocalRef(modClass);
		return matches == 1 ? result : nullptr;
	}
}
