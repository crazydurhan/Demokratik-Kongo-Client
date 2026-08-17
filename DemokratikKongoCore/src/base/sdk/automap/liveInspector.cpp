#include "liveInspector.h"
#include "automapUtil.h"
#include "classIndex.h"

#include "../../java/java.h"
#include "../jniResolve.h"
#include "../../util/logger.h"

#include <cstring>

namespace
{
	void clearEx(JNIEnv* env)
	{
		if (env && env->ExceptionCheck())
			env->ExceptionClear();
	}

	char primFromDesc(const std::string& desc)
	{
		if (desc.empty())
			return 'L';
		if (desc[0] == '[')
			return '[';
		if (desc.size() == 1)
			return desc[0];
		return 'L';
	}

	bool isPrimitiveOnlyDesc(const std::string& desc)
	{
		for (char c : desc)
		{
			if (c == 'L' || c == '[')
				return false;
		}
		return true;
	}
}

jclass LiveInspector::FindMinecraftFromClientThread(JNIEnv* env)
{
	if (!env)
		return nullptr;

	JniResolve::LocalFrame frame(env, 64);
	jclass cThread = env->FindClass("java/lang/Thread");
	if (!cThread) { clearEx(env); return nullptr; }

	jmethodID getAll = env->GetStaticMethodID(cThread, "getAllStackTraces", "()Ljava/util/Map;");
	jmethodID getName = env->GetMethodID(cThread, "getName", "()Ljava/lang/String;");
	jmethodID getStack = env->GetMethodID(cThread, "getStackTrace", "()[Ljava/lang/StackTraceElement;");
	if (!getAll || !getName || !getStack) { clearEx(env); return nullptr; }

	jclass cMap = env->FindClass("java/util/Map");
	jclass cSet = env->FindClass("java/util/Set");
	if (!cMap || !cSet) { clearEx(env); return nullptr; }
	jmethodID keySet = env->GetMethodID(cMap, "keySet", "()Ljava/util/Set;");
	jmethodID toArray = env->GetMethodID(cSet, "toArray", "()[Ljava/lang/Object;");
	if (!keySet || !toArray) { clearEx(env); return nullptr; }

	jclass cSte = env->FindClass("java/lang/StackTraceElement");
	if (!cSte) { clearEx(env); return nullptr; }
	jmethodID steClassName = env->GetMethodID(cSte, "getClassName", "()Ljava/lang/String;");
	if (!steClassName) { clearEx(env); return nullptr; }

	jobject map = env->CallStaticObjectMethod(cThread, getAll);
	clearEx(env);
	if (!map)
		return nullptr;
	jobject set = env->CallObjectMethod(map, keySet);
	if (!set) { clearEx(env); return nullptr; }
	jobjectArray threads = (jobjectArray)env->CallObjectMethod(set, toArray);
	if (!threads) { clearEx(env); return nullptr; }

	const jsize n = env->GetArrayLength(threads);
	jclass found = nullptr;
	for (jsize i = 0; i < n && !found; ++i)
	{
		jobject th = env->GetObjectArrayElement(threads, i);
		if (!th) continue;
		jstring jn = (jstring)env->CallObjectMethod(th, getName);
		clearEx(env);
		bool isClient = false;
		if (jn)
		{
			const char* utf = env->GetStringUTFChars(jn, nullptr);
			if (utf && std::strcmp(utf, "Client thread") == 0)
				isClient = true;
			if (utf) env->ReleaseStringUTFChars(jn, utf);
			env->DeleteLocalRef(jn);
		}
		if (!isClient)
		{
			env->DeleteLocalRef(th);
			continue;
		}

		jobjectArray stack = (jobjectArray)env->CallObjectMethod(th, getStack);
		clearEx(env);
		if (stack)
		{
			const jsize sc = env->GetArrayLength(stack);
			for (jsize s = 0; s < sc && !found; ++s)
			{
				jobject el = env->GetObjectArrayElement(stack, s);
				if (!el) continue;
				jstring cn = (jstring)env->CallObjectMethod(el, steClassName);
				clearEx(env);
				if (cn)
				{
					const char* utf = env->GetStringUTFChars(cn, nullptr);
					if (utf)
					{
						std::string slash = AutoMapUtil::ToSlash(utf);
						if (!AutoMapUtil::IsJdkPath(slash)
							&& slash != "net/minecraft/client/main/Main"
							&& slash.find("launchwrapper") == std::string::npos)
						{
							jclass cls = ClassIndex::Find(slash.c_str());
							if (cls)
								found = static_cast<jclass>(env->NewLocalRef(cls));
							else
							{
								// Class may be loaded but not yet indexed under this path.
								jclass loaded = env->FindClass(slash.c_str());
								clearEx(env);
								if (loaded)
									found = loaded;
							}
						}
						env->ReleaseStringUTFChars(cn, utf);
					}
					env->DeleteLocalRef(cn);
				}
				env->DeleteLocalRef(el);
			}
			env->DeleteLocalRef(stack);
		}
		env->DeleteLocalRef(th);
	}

	return found;
}

bool LiveInspector::BindClassLoaderFrom(JNIEnv* env, jclass cls)
{
	if (!env || !cls)
		return false;
	jclass classClass = env->FindClass("java/lang/Class");
	if (!classClass) { clearEx(env); return false; }
	jmethodID getLoader = env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
	env->DeleteLocalRef(classClass);
	if (!getLoader) { clearEx(env); return false; }

	jobject loader = env->CallObjectMethod(cls, getLoader);
	clearEx(env);
	if (!loader)
		return false;

	// Bootstrap-loaded classes (vanilla Notch) have a null loader.
	// Thread context loader is bound by the caller in that case.
	env->DeleteLocalRef(loader);
	return true;
}

int LiveInspector::SuperDepth(JNIEnv* env, jclass cls)
{
	if (!env || !cls)
		return 0;
	int depth = 0;
	jclass cur = static_cast<jclass>(env->NewLocalRef(cls));
	while (cur && depth < 16)
	{
		jclass parent = env->GetSuperclass(cur);
		clearEx(env);
		env->DeleteLocalRef(cur);
		cur = parent;
		if (cur)
			++depth;
	}
	if (cur)
		env->DeleteLocalRef(cur);
	return depth;
}

std::string LiveInspector::ClassSlashName(JNIEnv* env, jclass cls)
{
	if (!env || !cls)
		return {};
	if (Java::tiEnv)
	{
		std::string fromTi = ClassIndex::SignatureOf(env, Java::tiEnv, cls);
		if (!fromTi.empty())
			return fromTi;
	}
	jclass classClass = env->FindClass("java/lang/Class");
	if (!classClass) { clearEx(env); return {}; }
	jmethodID getName = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
	env->DeleteLocalRef(classClass);
	if (!getName) { clearEx(env); return {}; }
	jstring jn = (jstring)env->CallObjectMethod(cls, getName);
	clearEx(env);
	if (!jn)
		return {};
	const char* utf = env->GetStringUTFChars(jn, nullptr);
	std::string name = utf ? AutoMapUtil::ToSlash(utf) : "";
	if (utf) env->ReleaseStringUTFChars(jn, utf);
	env->DeleteLocalRef(jn);
	return name;
}

ClassShape LiveInspector::Measure(JNIEnv* env, jclass cls)
{
	ClassShape shape;
	if (!env || !cls)
		return shape;

	shape.superDepth = SuperDepth(env, cls);
	const auto fields = ListFields(env, cls, true);
	shape.fieldCount = (int)fields.size();
	shape.primSeq.reserve(fields.size());
	for (const auto& f : fields)
	{
		if (f.isStatic)
		{
			if (f.prim == 'L' && f.typeClass && env->IsSameObject(f.typeClass, cls))
				shape.staticSelf = true;
			continue;
		}
		shape.primSeq.push_back(f.prim);
		switch (f.prim)
		{
		case 'D': ++shape.d; break;
		case 'F': ++shape.f; break;
		case 'I': ++shape.i; break;
		case 'Z': ++shape.z; break;
		case 'J': ++shape.j; break;
		case '[': ++shape.arr; break;
		default:  ++shape.L; break;
		}
	}

	jclass classClass = env->FindClass("java/lang/Class");
	if (!classClass) { clearEx(env); return shape; }
	jmethodID getIfaces = env->GetMethodID(classClass, "getInterfaces", "()[Ljava/lang/Class;");
	jmethodID getMethods = env->GetMethodID(classClass, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
	env->DeleteLocalRef(classClass);
	if (getIfaces)
	{
		jobjectArray ifaces = (jobjectArray)env->CallObjectMethod(cls, getIfaces);
		clearEx(env);
		if (ifaces)
		{
			shape.ifaceCount = env->GetArrayLength(ifaces);
			env->DeleteLocalRef(ifaces);
		}
	}

	if (getMethods)
	{
		jclass methodClass = env->FindClass("java/lang/reflect/Method");
		if (methodClass)
		{
			jmethodID getRet = env->GetMethodID(methodClass, "getReturnType", "()Ljava/lang/Class;");
			jmethodID getParams = env->GetMethodID(methodClass, "getParameterTypes", "()[Ljava/lang/Class;");
			jobjectArray methods = (jobjectArray)env->CallObjectMethod(cls, getMethods);
			clearEx(env);
			if (methods && getRet && getParams)
			{
				const jsize mc = env->GetArrayLength(methods);
				for (jsize i = 0; i < mc; ++i)
				{
					jobject m = env->GetObjectArrayElement(methods, i);
					if (!m) continue;
					std::string desc = "(";
					jobjectArray params = (jobjectArray)env->CallObjectMethod(m, getParams);
					clearEx(env);
					if (params)
					{
						const jsize pc = env->GetArrayLength(params);
						for (jsize j = 0; j < pc; ++j)
						{
							jclass pt = (jclass)env->GetObjectArrayElement(params, j);
							if (pt)
							{
								desc += JniResolve::ClassToDescriptor(env, pt);
								env->DeleteLocalRef(pt);
							}
						}
						env->DeleteLocalRef(params);
					}
					desc += ")";
					jclass rt = (jclass)env->CallObjectMethod(m, getRet);
					clearEx(env);
					if (rt)
					{
						desc += JniResolve::ClassToDescriptor(env, rt);
						env->DeleteLocalRef(rt);
					}
					if (isPrimitiveOnlyDesc(desc))
						shape.methodPrims.push_back(desc);
					env->DeleteLocalRef(m);
				}
				env->DeleteLocalRef(methods);
			}
			env->DeleteLocalRef(methodClass);
		}
	}

	return shape;
}

std::vector<InspectedField> LiveInspector::ListFields(JNIEnv* env, jclass cls, bool declaredOnly)
{
	std::vector<InspectedField> out;
	if (!env || !cls)
		return out;

	jclass classClass = env->FindClass("java/lang/Class");
	if (!classClass) { clearEx(env); return out; }
	jmethodID getFields = env->GetMethodID(classClass,
		declaredOnly ? "getDeclaredFields" : "getFields",
		"()[Ljava/lang/reflect/Field;");
	env->DeleteLocalRef(classClass);
	if (!getFields) { clearEx(env); return out; }

	jclass fieldClass = env->FindClass("java/lang/reflect/Field");
	if (!fieldClass) { clearEx(env); return out; }
	jmethodID getName = env->GetMethodID(fieldClass, "getName", "()Ljava/lang/String;");
	jmethodID getType = env->GetMethodID(fieldClass, "getType", "()Ljava/lang/Class;");
	jmethodID getMods = env->GetMethodID(fieldClass, "getModifiers", "()I");
	jclass modClass = env->FindClass("java/lang/reflect/Modifier");
	jmethodID isStatic = modClass ? env->GetStaticMethodID(modClass, "isStatic", "(I)Z") : nullptr;
	if (!getName || !getType || !getMods)
	{
		clearEx(env);
		env->DeleteLocalRef(fieldClass);
		if (modClass) env->DeleteLocalRef(modClass);
		return out;
	}

	jobjectArray fields = (jobjectArray)env->CallObjectMethod(cls, getFields);
	clearEx(env);
	if (!fields)
	{
		env->DeleteLocalRef(fieldClass);
		if (modClass) env->DeleteLocalRef(modClass);
		return out;
	}

	const jsize n = env->GetArrayLength(fields);
	out.reserve(n);
	for (jsize i = 0; i < n; ++i)
	{
		jobject f = env->GetObjectArrayElement(fields, i);
		if (!f) continue;
		InspectedField info;
		jstring jn = (jstring)env->CallObjectMethod(f, getName);
		if (jn)
		{
			const char* utf = env->GetStringUTFChars(jn, nullptr);
			if (utf) info.name = utf;
			if (utf) env->ReleaseStringUTFChars(jn, utf);
			env->DeleteLocalRef(jn);
		}
		jint mods = env->CallIntMethod(f, getMods);
		clearEx(env);
		if (isStatic)
			info.isStatic = env->CallStaticBooleanMethod(modClass, isStatic, mods) == JNI_TRUE;
		jclass t = (jclass)env->CallObjectMethod(f, getType);
		clearEx(env);
		if (t)
		{
			const std::string desc = JniResolve::ClassToDescriptor(env, t);
			info.prim = primFromDesc(desc);
			if (info.prim == 'L' && desc.size() > 2)
				info.typeSlash = desc.substr(1, desc.size() - 2);
			else if (info.prim == '[')
				info.typeSlash = desc;
			info.typeClass = t;
		}
		info.id = env->FromReflectedField(f);
		clearEx(env);
		out.push_back(std::move(info));
		env->DeleteLocalRef(f);
	}
	env->DeleteLocalRef(fields);
	env->DeleteLocalRef(fieldClass);
	if (modClass) env->DeleteLocalRef(modClass);
	return out;
}

std::string LiveInspector::FieldRuntimeName(JNIEnv* env, jclass cls, jfieldID id)
{
	if (!env || !cls || !id)
		return {};
	const auto fields = ListFields(env, cls, false);
	for (const auto& f : fields)
	{
		if (f.id == id)
			return f.name;
	}
	return {};
}

std::string LiveInspector::MethodRuntimeName(JNIEnv* env, jclass cls, jmethodID id, const char* sig)
{
	if (!env || !cls || !id)
		return {};
	jclass classClass = env->FindClass("java/lang/Class");
	if (!classClass) { clearEx(env); return {}; }
	jmethodID getMethods = env->GetMethodID(classClass, "getDeclaredMethods", "()[Ljava/lang/reflect/Method;");
	env->DeleteLocalRef(classClass);
	if (!getMethods) { clearEx(env); return {}; }

	jclass methodClass = env->FindClass("java/lang/reflect/Method");
	if (!methodClass) { clearEx(env); return {}; }
	jmethodID getName = env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;");
	if (!getName) { clearEx(env); env->DeleteLocalRef(methodClass); return {}; }

	jobjectArray methods = (jobjectArray)env->CallObjectMethod(cls, getMethods);
	clearEx(env);
	std::string result;
	if (methods)
	{
		const jsize n = env->GetArrayLength(methods);
		for (jsize i = 0; i < n && result.empty(); ++i)
		{
			jobject m = env->GetObjectArrayElement(methods, i);
			if (!m) continue;
			jmethodID mid = env->FromReflectedMethod(m);
			clearEx(env);
			if (mid == id)
			{
				jstring jn = (jstring)env->CallObjectMethod(m, getName);
				if (jn)
				{
					const char* utf = env->GetStringUTFChars(jn, nullptr);
					if (utf) result = utf;
					if (utf) env->ReleaseStringUTFChars(jn, utf);
					env->DeleteLocalRef(jn);
				}
			}
			env->DeleteLocalRef(m);
		}
		env->DeleteLocalRef(methods);
	}
	env->DeleteLocalRef(methodClass);
	(void)sig;
	return result;
}

jobject LiveInspector::TryGetMinecraftInstance(JNIEnv* env, jclass mcClass, jmethodID getMinecraft)
{
	if (!env || !mcClass || !getMinecraft)
		return nullptr;
	jobject inst = env->CallStaticObjectMethod(mcClass, getMinecraft);
	clearEx(env);
	return inst;
}

void LiveInspector::DisambiguateFromInstance(JNIEnv* env, jclass mcClass, jobject mc)
{
	(void)env;
	(void)mcClass;
	(void)mc;
	// Binding of live-disambiguated members happens in ConstraintSolver.
}
