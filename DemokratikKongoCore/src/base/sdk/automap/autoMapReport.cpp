#include "autoMapReport.h"
#include "nameScheme.h"

#include "../../launcher/launcherDetection.h"
#include "../../util/logger.h"

#include "stealthNames.h"

#include <Windows.h>
#include <fstream>
#include <sstream>

namespace
{
	std::vector<AutoMapEntry> g_entries;
	std::string g_path;

	const char* kindName(ReportKind k)
	{
		switch (k)
		{
		case ReportKind::Field:  return "FIELD";
		case ReportKind::Method: return "METHOD";
		default:                 return "CLASS";
		}
	}
}

const char* AutoMapReport::ViaName(ResolveVia via)
{
	switch (via)
	{
	case ResolveVia::Table:        return "table";
	case ResolveVia::Descriptor:   return "descriptor";
	case ResolveVia::ClientThread: return "client-thread";
	case ResolveVia::Constraint:   return "constraint";
	case ResolveVia::Score:        return "score";
	case ResolveVia::Live:         return "live";
	case ResolveVia::SuperChain:   return "super-chain";
	case ResolveVia::UniqueType:   return "unique-type";
	case ResolveVia::PrimSeq:      return "primSeq";
	case ResolveVia::StaticSelf:   return "static-self";
	case ResolveVia::ClassIndex:   return "class-index";
	default:                       return "none";
	}
}

void AutoMapReport::Reset()
{
	g_entries.clear();
	g_path.clear();
}

void AutoMapReport::Add(const AutoMapEntry& entry)
{
	g_entries.push_back(entry);
}

int AutoMapReport::FoundCount()
{
	int n = 0;
	for (const auto& e : g_entries)
		if (e.found) ++n;
	return n;
}

int AutoMapReport::MissingCount()
{
	return TotalCount() - FoundCount();
}

int AutoMapReport::TotalCount()
{
	return static_cast<int>(g_entries.size());
}

const std::string& AutoMapReport::FilePath()
{
	return g_path;
}

void AutoMapReport::Dump()
{
	char tempPath[MAX_PATH] = {};
	GetTempPathA(MAX_PATH, tempPath);
	g_path = std::string(tempPath) + dk::stealth::kAppFolderUtf8 + "\\";
	CreateDirectoryA(g_path.c_str(), nullptr);
	g_path += "automap-report.txt";

	const int found = FoundCount();
	const int total = TotalCount();
	const int missing = total - found;

	std::ostringstream body;
	body << "Launcher=" << LauncherDetection::KindName(LauncherDetection::Kind())
		<< "  NameScheme=" << NameSchemeDetect::SchemeName(NameSchemeDetect::Scheme())
		<< "\nFOUND  " << found << "/" << total
		<< "\nMISSING " << missing << "/" << total
		<< "\n\n";

	auto dumpKind = [&](ReportKind kind) {
		body << "[" << kindName(kind) << "]\n";
		for (const auto& e : g_entries)
		{
			if (e.kind != kind)
				continue;
			body << (e.found ? "  OK   " : "  MISS ");
			if (!e.owner.empty())
				body << e.owner;
			if (!e.mcpName.empty())
			{
				if (!e.owner.empty())
					body << ".";
				body << e.mcpName;
			}
			if (!e.runtimeName.empty())
				body << "  runtime=" << e.runtimeName;
			if (!e.sig.empty())
				body << "  sig=" << e.sig;
			if (!e.via.empty())
				body << "  via=" << e.via;
			if (e.found)
				body << "  conf=" << e.confidence;
			if (!e.found && !e.failReason.empty())
				body << "  " << e.failReason;
			if (e.critical)
				body << "  [critical]";
			body << "\n";
		}
		body << "\n";
	};

	dumpKind(ReportKind::Class);
	dumpKind(ReportKind::Field);
	dumpKind(ReportKind::Method);

	std::ofstream out(g_path, std::ios::out | std::ios::trunc);
	if (out)
	{
		out << body.str();
		out.close();
	}

	std::string missingList;
	for (const auto& e : g_entries)
	{
		if (e.found)
			continue;
		if (!missingList.empty())
			missingList += ", ";
		if (!e.mcpName.empty())
			missingList += e.mcpName;
		else
			missingList += e.owner;
	}

	const std::string summary = std::string("[AutoMap] ")
		+ LauncherDetection::KindName(LauncherDetection::Kind()) + "/"
		+ NameSchemeDetect::SchemeName(NameSchemeDetect::Scheme())
		+ "  FOUND " + std::to_string(found) + "/" + std::to_string(total)
		+ "  MISSING " + std::to_string(missing)
		+ "  report=" + g_path;

	bool criticalMiss = false;
	for (const auto& e : g_entries)
	{
		if (!e.found && e.critical)
			criticalMiss = true;
	}

	if (criticalMiss)
		Logger::Warn("AutoMap", summary);
	else
		Logger::Info("AutoMap", summary);

	if (!missingList.empty())
		Logger::Info("AutoMap", std::string("MISSING: ") + missingList);
}
