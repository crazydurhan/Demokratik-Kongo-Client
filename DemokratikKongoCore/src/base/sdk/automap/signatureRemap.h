#pragma once

#include <string>

namespace SignatureRemap
{
	void Clear();
	void Bind(const char* mcpSlash, const char* runtimeSlash);
	const char* RuntimeName(const char* mcpSlash);
	std::string Apply(const char* sig);
}
