#pragma once

#include "../../../../ext/jni/jni.h"
#include "../../java/java.h"

#include <string>
#include <map>

class JObjectWrapper
{
private:
	jobject m_instance = nullptr;

	void release()
	{
		if (m_instance)
		{
			JNIEnv* env = Java::Env;
			if (env)
			{
				env->DeleteGlobalRef(m_instance);
			}
			m_instance = nullptr;
		}
	}

public:
	JObjectWrapper() : m_instance(nullptr) {}

	explicit JObjectWrapper(jobject instance)
	{
		if (instance)
		{
			JNIEnv* env = Java::Env;
			if (env)
			{
				m_instance = env->NewGlobalRef(instance);
			}
		}
	}

	JObjectWrapper(const JObjectWrapper& other)
	{
		if (other.m_instance)
		{
			JNIEnv* env = Java::Env;
			if (env)
			{
				m_instance = env->NewGlobalRef(other.m_instance);
			}
		}
	}

	JObjectWrapper(JObjectWrapper&& other) noexcept
	{
		m_instance = other.m_instance;
		other.m_instance = nullptr;
	}

	~JObjectWrapper()
	{
		release();
	}

	JObjectWrapper& operator=(const JObjectWrapper& other)
	{
		if (this != &other)
		{
			release();
			if (other.m_instance)
			{
				JNIEnv* env = Java::Env;
				if (env)
				{
					m_instance = env->NewGlobalRef(other.m_instance);
				}
			}
		}
		return *this;
	}

	JObjectWrapper& operator=(JObjectWrapper&& other) noexcept
	{
		if (this != &other)
		{
			release();
			m_instance = other.m_instance;
			other.m_instance = nullptr;
		}
		return *this;
	}

	JObjectWrapper& operator=(jobject instance)
	{
		release();
		if (instance)
		{
			JNIEnv* env = Java::Env;
			if (env)
			{
				m_instance = env->NewGlobalRef(instance);
			}
		}
		return *this;
	}

	operator jobject() const { return m_instance; }
	jobject Get() const { return m_instance; }
	bool IsValid() const { return m_instance != nullptr; }
};

struct IClass
{
	std::map<std::string, jmethodID> MethodIDs;
	std::map<std::string, jfieldID> FieldIDs;

	JObjectWrapper Instance;
	jclass Class = 0;

	virtual ~IClass() = default;

	virtual jobject GetInstance() = 0;
	virtual jclass GetClass() = 0;
};


