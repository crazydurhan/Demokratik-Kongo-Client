#pragma once

#include <string>
#include <vector>

struct ClassShape
{
	std::string primSeq;
	int d = 0, f = 0, i = 0, z = 0, j = 0, L = 0, arr = 0;
	std::vector<std::string> methodPrims;
	int superDepth = 0;
	bool staticSelf = false;
	int ifaceCount = 0;
	int fieldCount = 0;
};

struct ShapeRef
{
	const char* mcp;
	const char* distinctiveSeq; // subsequence that should appear in primSeq
	int minD, minF, minI, minZ, minL;
	int superDepth;
	bool staticSelf;
	int minFields;
};

namespace ShapeTable
{
	const ShapeRef* Refs();
	unsigned int RefCount();
	const ShapeRef* FindRef(const char* mcp);

	float HistCosine(const ClassShape& a, const ClassShape& b);
	float MethodJaccard(const ClassShape& a, const ClassShape& b);
	int LcsLength(const std::string& a, const std::string& b);
	bool ContainsSeq(const std::string& hay, const char* needle);
	float Score(const ShapeRef& ref, const ClassShape& live);
}
