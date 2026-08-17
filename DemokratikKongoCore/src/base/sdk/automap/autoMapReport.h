#pragma once

#include <string>
#include <vector>

enum class ResolveVia : unsigned char
{
	None = 0,
	Table,
	Descriptor,
	ClientThread,
	Constraint,
	Score,
	Live,
	SuperChain,
	UniqueType,
	PrimSeq,
	StaticSelf,
	ClassIndex
};

enum class ReportKind : unsigned char
{
	Class = 0,
	Field,
	Method
};

struct AutoMapEntry
{
	ReportKind kind = ReportKind::Class;
	std::string owner;
	std::string mcpName;
	std::string runtimeName;
	std::string sig;
	std::string via;
	std::string failReason;
	float confidence = 0.f;
	bool found = false;
	bool critical = false;
};

namespace AutoMapReport
{
	void Reset();
	void Add(const AutoMapEntry& entry);
	void Dump();
	const std::string& FilePath();
	const char* ViaName(ResolveVia via);
	int FoundCount();
	int MissingCount();
	int TotalCount();
}
