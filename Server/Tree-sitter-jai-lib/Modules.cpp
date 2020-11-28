#include <filesystem>
#include <fstream>

#include "TreeSitterJai.h"
#include "FileScope.h"
#include "Timer.h"
#include "Concurrent.h"

ConcurrentVector<std::string> g_modulePaths;

std::optional<ScopeDeclaration> Module::Search(Hash hash)
{
	//return exportedScope.TryGet(hash);
	// right now we're not building the module so instead search loaded files
	return moduleFile->SearchExports(hash);
}

int Module::SearchAndGetFile(Hash hash, FileScope** outFile, Scope** declScope)
{
	return moduleFile->SearchAndGetExport(hash, outFile, declScope);
}



export_jai_lsp long long CreateTreeFromPath(const char* document, const char* moduleName)
{
	// for modules
	auto timer = Timer("");

	std::ifstream t(document);
	t.seekg(0, std::ios::end);
	size_t size = t.tellg();
	std::string buffer(size, ' ');
	t.seekg(0);
	t.read(&buffer[0], size);

	CreateTree(document, buffer.c_str(), (int)buffer.length());

	return timer.GetMicroseconds();
}


export_jai_lsp void AddModuleDirectory(const char* moduleDirectory)
{
	g_modulePaths.Append(moduleDirectory);

	// see if we can find preload
	auto path = std::filesystem::path(moduleDirectory);
	path = path.append("Preload.jai");

	if (std::filesystem::exists(path))
	{
		FileScope::preloadHash = StringHash(path.string());
		if (!g_filePaths.Read(FileScope::preloadHash))
		{
			g_filePaths.Write(FileScope::preloadHash, path.string());
		}
	}
}


bool ModuleFileExists(std::string name)
{
	auto modulePathCount = g_modulePaths.size();

	if (modulePathCount == 0)
		return false;

	for (int i = 0; i < modulePathCount; i++)
	{
		auto path = std::filesystem::path(g_modulePaths.Read(i));
		path = path.append(name);
		path = path.append("module.jai");

		if (std::filesystem::exists(path))
		{
			return true;
		}
	}

	return false;
}




std::optional<std::filesystem::path> ModuleFilePath(std::string name)
{
	auto modulePathCount = g_modulePaths.size();
	assert(modulePathCount > 0);

	if (modulePathCount == 0)
		return std::nullopt;

	for (int i = 0; i < modulePathCount; i++)
	{
		auto path = std::filesystem::path(g_modulePaths.Read(i));
		path = path.append(name);
		path = path.append("module.jai");

		if (std::filesystem::exists(path))
		{
			return path;
		}
	}

	return std::nullopt;
}


Module* RegisterModule(std::string moduleName, std::filesystem::path path)
{
	auto pathStr = path.string();
	auto documentHash = StringHash(pathStr);

	auto scope = new FileScope();
	scope->documentHash = documentHash;

	scope->fileIndex = (uint16_t)g_fileScopeByIndex.size();
	g_fileScopeByIndex.Append(scope);
	g_fileScopes.Write(documentHash, scope);
	g_filePaths.Write(documentHash, pathStr);

	auto mod = new Module();
	mod->moduleFile = scope;
	mod->moduleFileHash = documentHash;

	auto moduleNameHash = StringHash(moduleName);
	g_modules.Write(moduleNameHash, mod);

	return mod;
}