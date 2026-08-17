#include "signatureRemap.h"

#include <cstring>
#include <mutex>
#include <unordered_map>

namespace
{
	std::mutex g_mutex;
	std::unordered_map<std::string, std::string> g_mcpToRuntime;
}

void SignatureRemap::Clear()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_mcpToRuntime.clear();
}

void SignatureRemap::Bind(const char* mcpSlash, const char* runtimeSlash)
{
	if (!mcpSlash || !runtimeSlash || !*mcpSlash || !*runtimeSlash)
		return;
	std::lock_guard<std::mutex> lock(g_mutex);
	g_mcpToRuntime[mcpSlash] = runtimeSlash;
}

const char* SignatureRemap::RuntimeName(const char* mcpSlash)
{
	if (!mcpSlash)
		return nullptr;
	std::lock_guard<std::mutex> lock(g_mutex);
	auto it = g_mcpToRuntime.find(mcpSlash);
	return it == g_mcpToRuntime.end() ? nullptr : it->second.c_str();
}

std::string SignatureRemap::Apply(const char* sig)
{
	if (!sig)
		return {};
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_mcpToRuntime.empty())
		return sig;

	std::string out;
	out.reserve(strlen(sig) + 8);
	for (const char* p = sig; *p; ++p)
	{
		if (*p == 'L')
		{
			const char* end = strchr(p, ';');
			if (end)
			{
				std::string cls(p + 1, end - p - 1);
				auto it = g_mcpToRuntime.find(cls);
				if (it != g_mcpToRuntime.end() && it->second != cls)
				{
					out += 'L';
					out += it->second;
					out += ';';
					p = end;
					continue;
				}
			}
		}
		out += *p;
	}
	return out;
}
