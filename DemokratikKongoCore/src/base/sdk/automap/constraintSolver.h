#pragma once

#include "../../../../ext/jni/jni.h"
#include "autoMapReport.h"

#include <string>
#include <vector>

struct SolvedClass
{
	std::string mcp;
	jclass cls = nullptr;
	ResolveVia via = ResolveVia::None;
	float confidence = 0.f;
	std::string runtimeName;
};

struct SolvedMember
{
	std::string owner;
	std::string mcpName;
	bool isField = true;
	bool isStatic = false;
	jfieldID fid = nullptr;
	jmethodID mid = nullptr;
	ResolveVia via = ResolveVia::None;
	float confidence = 0.f;
	std::string runtimeName;
	std::string sig;
};

namespace ConstraintSolver
{
	void Run(JNIEnv* env, jclass minecraft, std::vector<SolvedClass>& classes, std::vector<SolvedMember>& members);
}
