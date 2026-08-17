#pragma once

#include <cstring>
#include <string>

namespace AutoMapUtil
{
	inline std::string ToSlash(const char* name)
	{
		std::string out = name ? name : "";
		for (char& c : out)
			if (c == '.') c = '/';
		return out;
	}

	inline std::string ToDot(const char* name)
	{
		std::string out = name ? name : "";
		for (char& c : out)
			if (c == '/') c = '.';
		return out;
	}

	inline bool StartsWith(const std::string& s, const char* prefix)
	{
		const size_t n = prefix ? strlen(prefix) : 0;
		return n && s.size() >= n && s.compare(0, n, prefix) == 0;
	}

	inline bool IsJdkPath(const std::string& slash)
	{
		return StartsWith(slash, "java/")
			|| StartsWith(slash, "javax/")
			|| StartsWith(slash, "sun/")
			|| StartsWith(slash, "com/sun/")
			|| StartsWith(slash, "jdk/")
			|| StartsWith(slash, "org/lwjgl/")
			|| StartsWith(slash, "io/netty/")
			|| StartsWith(slash, "com/google/")
			|| StartsWith(slash, "org/apache/")
			|| StartsWith(slash, "com/mojang/")
			|| StartsWith(slash, "org/objectweb/")
			|| StartsWith(slash, "it/unimi/");
	}
}
