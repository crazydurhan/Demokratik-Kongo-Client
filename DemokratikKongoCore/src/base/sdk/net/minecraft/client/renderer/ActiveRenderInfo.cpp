#include "ActiveRenderInfo.h"

#include "../../../../../java/java.h"
#include "../../../../sdk.h"
#include "../../../../java/nio/FloatBuffer.h"
#include "../../../../strayCache.h"
#include "../../../../jniResolve.h"

CActiveRenderInfo::CActiveRenderInfo()
{
	JNIEnv* env = Java::GetEnv();
	if (!Java::AssignClass("net.minecraft.client.renderer.ActiveRenderInfo", this->Class) || !env)
		return;

	this->FieldIDs["PROJECTION"] = JniResolve::Field(env, this->Class, "Ljava/nio/FloatBuffer;", "PROJECTION");
	this->FieldIDs["MODELVIEW"] = JniResolve::Field(env, this->Class, "Ljava/nio/FloatBuffer;", "MODELVIEW");
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
	if (!env || !this->Class || !fid)
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
	if (!env || !this->Class || !fid)
		return Matrix{};

	jobject modelView = env->GetStaticObjectField(this->GetClass(), fid);
	JniResolve::ClearException(env);
	if (!modelView)
		return Matrix{};

	Matrix m = FloatBuffer::FloatBuffer(modelView).GetMatrix();
	env->DeleteLocalRef(modelView);
	return m;
}
