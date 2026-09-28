#include "ActiveRenderInfo.h"

#include "../../../../../java/java.h"
#include "../../../../sdk.h"
#include "../../../../java/nio/FloatBuffer.h"
#include "../../../../strayCache.h"
#include "../../../../jniResolve.h"
#include "../../../../../util/logger.h"

namespace
{
    jmethodID g_fbGet = nullptr;

    bool readBufferFloat16(JNIEnv* env, jobject buf, float out[16])
    {
        if (!env || !buf)
            return false;
        if (!g_fbGet)
        {
            jclass fbc = env->FindClass("java/nio/FloatBuffer");
            if (!fbc)
            {
                JniResolve::ClearException(env);
                return false;
            }
            g_fbGet = env->GetMethodID(fbc, "get", "(I)F");
            JniResolve::ClearException(env);
            env->DeleteLocalRef(fbc);
            if (!g_fbGet)
                return false;
        }
        for (int i = 0; i < 16; ++i)
        {
            out[i] = env->CallFloatMethod(buf, g_fbGet, (jint)i);
            if (env->ExceptionCheck())
            {
                env->ExceptionClear();
                return false;
            }
        }
        return true;
    }

    // PROJECTION/MODELVIEW are STATIC FloatBuffer fields (GetStaticFieldID,
    // never GetFieldID) whose obfuscated names differ per client. Identify
    // them by matrix shape instead of guessing names:
    //   projection: GL perspective -> [11] == -1 && [15] == 0
    //   modelview:  rigid transform -> [15] == 1
    // (OBJECTCOORDS has only 3 floats, so the 16-float read rejects it.)
    jfieldID findStaticMatrixBuffer(JNIEnv* env, jclass cls, bool wantProjection, std::string* outName = nullptr)
    {
        static const char* candidates[] = {
            "PROJECTION", "MODELVIEW", "OBJECTCOORDS",
            "a", "b", "c", "d",
            "field_178812_b", "field_178813_c", "field_178814_a"
        };
        for (const char* name : candidates)
        {
            jfieldID fid = env->GetStaticFieldID(cls, name, "Ljava/nio/FloatBuffer;");
            if (env->ExceptionCheck())
            {
                JniResolve::ClearException(env);
                continue;
            }
            if (!fid)
                continue;
            jobject buf = env->GetStaticObjectField(cls, fid);
            if (env->ExceptionCheck())
            {
                JniResolve::ClearException(env);
                if (buf) env->DeleteLocalRef(buf);
                continue;
            }
            float v[16] = { 0 };
            const bool ok = buf && readBufferFloat16(env, buf, v);
            if (buf)
                env->DeleteLocalRef(buf);
            if (!ok)
                continue;
            const bool isProjection = (v[11] == -1.0f && v[15] == 0.0f);
            const bool isModelView = (v[15] == 1.0f);
            if (wantProjection ? isProjection : isModelView)
            {
                if (outName)
                    *outName = name;
                return fid;
            }
        }
        return nullptr;
    }
}

CActiveRenderInfo::CActiveRenderInfo()
{
	JNIEnv* env = Java::GetEnv();
	if (!Java::AssignClass("net/minecraft/client/renderer.ActiveRenderInfo", this->Class) || !env)
		return;

	// Static fields: resolve lazily by shape (see findStaticMatrixBuffer).
	std::string projName, mvName;
	this->FieldIDs["PROJECTION"] = findStaticMatrixBuffer(env, this->Class, true, &projName);
	this->FieldIDs["MODELVIEW"] = findStaticMatrixBuffer(env, this->Class, false, &mvName);

	if (this->FieldIDs["PROJECTION"] && this->FieldIDs["MODELVIEW"])
	{
		Logger::Info("ActiveRenderInfo", "matrix buffers resolved: PROJECTION='" + projName
			+ "' MODELVIEW='" + mvName + "'");
	}
	else
	{
		Logger::Warn("ActiveRenderInfo", std::string("matrix buffer resolution incomplete: PROJECTION=")
			+ (this->FieldIDs["PROJECTION"] ? "ok" : "null")
			+ " MODELVIEW=" + (this->FieldIDs["MODELVIEW"] ? "ok" : "null"));
	}
}

jclass CActiveRenderInfo::GetClass()
{
	return this->Class;
}

jobject CActiveRenderInfo::GetInstance()
{
	return NULL;
}

Matrix CActiveRenderInfo::ProjectionMatrix()
{
	JNIEnv* env = Java::GetEnv();
	jfieldID fid = this->FieldIDs["PROJECTION"];
	if (!env || !this->Class)
		return Matrix{};
	if (!fid)
	{
		fid = findStaticMatrixBuffer(env, this->Class, true);
		this->FieldIDs["PROJECTION"] = fid;
	}
	if (!fid)
		return Matrix{};

	jobject projection = env->GetStaticObjectField(this->GetClass(), fid);
	JniResolve::ClearException(env);
	if (!projection)
		return Matrix{};

	Matrix m = FloatBuffer::FloatBuffer(projection).GetMatrix();
	env->DeleteLocalRef(projection);
	return m;
}

Matrix CActiveRenderInfo::ModelViewMatrix()
{
	JNIEnv* env = Java::GetEnv();
	jfieldID fid = this->FieldIDs["MODELVIEW"];
	if (!env || !this->Class)
		return Matrix{};
	if (!fid)
	{
		fid = findStaticMatrixBuffer(env, this->Class, false);
		this->FieldIDs["MODELVIEW"] = fid;
	}
	if (!fid)
		return Matrix{};

	jobject modelView = env->GetStaticObjectField(this->GetClass(), fid);
	JniResolve::ClearException(env);
	if (!modelView)
		return Matrix{};

	Matrix m = FloatBuffer::FloatBuffer(modelView).GetMatrix();
	env->DeleteLocalRef(modelView);
	return m;
}