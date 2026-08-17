#include "FloatBuffer.h"

#include "../../../java/java.h"
#include "../../../util/logger.h"
#include "../../jniResolve.h"

#include <vector>

FloatBuffer::FloatBuffer(jobject obj)
{
	this->Instance = obj;
	JNIEnv* env = Java::GetEnv();
	if (!env || !obj) return;

	jclass cls = env->FindClass("java/nio/FloatBuffer");
	if (!cls) return;
	this->Class = (jclass)env->NewGlobalRef(cls);
	env->DeleteLocalRef(cls);
	this->MethodIDs["get"] = env->GetMethodID(this->Class, "get", "(I)F");
	JniResolve::ClearException(env);
}

Matrix FloatBuffer::GetMatrix()
{
	JNIEnv* env = Java::GetEnv();
	jobject inst = GetInstance();
	jmethodID getMid = this->MethodIDs["get"];
	if (!env || !inst || !getMid)
		return Matrix{};

	std::vector<float> arr;
	arr.reserve(16);
	for (int i = 0; i < 16; i++)
	{
		float v = env->CallFloatMethod(inst, getMid, i);
		if (env->ExceptionCheck()) { env->ExceptionClear(); return Matrix{}; }
		arr.push_back(v);
	}

	Matrix m;
	m.m00 = arr[0];  m.m01 = arr[1];  m.m02 = arr[2];  m.m03 = arr[3];
	m.m10 = arr[4];  m.m11 = arr[5];  m.m12 = arr[6];  m.m13 = arr[7];
	m.m20 = arr[8];  m.m21 = arr[9];  m.m22 = arr[10]; m.m23 = arr[11];
	m.m30 = arr[12]; m.m31 = arr[13]; m.m32 = arr[14]; m.m33 = arr[15];
	return m;
}

jclass FloatBuffer::GetClass()
{
	return this->Class;
}

jobject FloatBuffer::GetInstance()
{
	return this->Instance;
}
