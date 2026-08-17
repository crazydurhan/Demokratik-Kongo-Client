#include "String.h"
#include "../../../java/java.h"

String::String(jobject instance)
{
	this->Instance = instance;
}

String::String(std::string text)
{
	jstring localStr = Java::Env->NewStringUTF(text.c_str());
	this->Instance = localStr;
	Java::Env->DeleteLocalRef(localStr);
}

jclass String::GetClass()
{
	return nullptr;
}

jobject String::GetInstance()
{
	return this->Instance;
}

std::string String::ToString()
{
	jstring jStr = (jstring)this->Instance.Get();
	if (!jStr) return "";
	const char* nativeStr = Java::Env->GetStringUTFChars(jStr, nullptr);
	std::string result = std::string(nativeStr);
	Java::Env->ReleaseStringUTFChars(jStr, nativeStr);
	return result;
}
