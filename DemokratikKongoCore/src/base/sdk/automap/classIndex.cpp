#include "classIndex.h"

#include "../../util/logger.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
	std::mutex g_mutex;
	std::unordered_map<std::string, jclass> g_byPath;
	bool g_ready = false;

	void clearLocked(JNIEnv* env)
	{
		if (env)
		{
			for (auto& pair : g_byPath)
			{
				if (pair.second)
					env->DeleteGlobalRef(pair.second);
			}
		}
		g_byPath.clear();
		g_ready = false;
	}

	bool shouldSkipSignature(const std::string& sig)
	{
		if (sig.size() < 3 || sig.front() != 'L' || sig.back() != ';')
			return true;
		if (sig[1] == '[')
			return true;
		return false;
	}
}

bool ClassIndex::Build(JNIEnv* env, jvmtiEnv* ti)
{
	if (!env || !ti)
		return false;

	std::lock_guard<std::mutex> lock(g_mutex);
	clearLocked(env);

	jint count = 0;
	jclass* classes = nullptr;
	if (ti->GetLoadedClasses(&count, &classes) != JVMTI_ERROR_NONE || !classes)
		return false;

	g_byPath.reserve(static_cast<size_t>(count));
	for (jint i = 0; i < count; ++i)
	{
		char* sigBuf = nullptr;
		if (ti->GetClassSignature(classes[i], &sigBuf, nullptr) == JVMTI_ERROR_NONE && sigBuf)
		{
			std::string sig = sigBuf;
			ti->Deallocate(reinterpret_cast<unsigned char*>(sigBuf));
			if (!shouldSkipSignature(sig))
			{
				std::string path = sig.substr(1, sig.size() - 2);
				if (g_byPath.find(path) == g_byPath.end())
					g_byPath.emplace(std::move(path), static_cast<jclass>(env->NewGlobalRef(classes[i])));
			}
		}
		env->DeleteLocalRef(classes[i]);
	}
	ti->Deallocate(reinterpret_cast<unsigned char*>(classes));

	g_ready = !g_byPath.empty();
	Logger::Log("[ClassIndex] indexed " + std::to_string(g_byPath.size()) + " classes");
	return g_ready;
}

void ClassIndex::Shutdown(JNIEnv* env)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	clearLocked(env);
}

bool ClassIndex::Ready()
{
	return g_ready;
}

size_t ClassIndex::Size()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_byPath.size();
}

jclass ClassIndex::Find(const char* slashPath)
{
	if (!slashPath || !*slashPath)
		return nullptr;
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_byPath.find(slashPath);
	return it == g_byPath.end() ? nullptr : it->second;
}

void ClassIndex::Add(const char* slashPath, jclass globalCls)
{
	if (!slashPath || !*slashPath || !globalCls)
		return;
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_byPath.find(slashPath);
	if (it != g_byPath.end())
		return;
	g_byPath.emplace(slashPath, globalCls);
}

std::string ClassIndex::SignatureOf(JNIEnv* env, jvmtiEnv* ti, jclass cls)
{
	if (!ti || !cls)
		return {};
	char* sigBuf = nullptr;
	if (ti->GetClassSignature(cls, &sigBuf, nullptr) != JVMTI_ERROR_NONE || !sigBuf)
		return {};
	std::string sig = sigBuf;
	ti->Deallocate(reinterpret_cast<unsigned char*>(sigBuf));
	if (sig.size() >= 3 && sig.front() == 'L' && sig.back() == ';')
		return sig.substr(1, sig.size() - 2);
	(void)env;
	return {};
}

void ClassIndex::ForEach(const std::function<void(const std::string&, jclass)>& fn)
{
	if (!fn)
		return;
	std::lock_guard<std::mutex> lock(g_mutex);
	for (const auto& pair : g_byPath)
		fn(pair.first, pair.second);
}
