#include "TreeSitterJai.h"
#include "FileScope.h"
#include "Timer.h"
#include <filesystem>
#include <fstream>



void Module::BuildExportedScope()
{


}

std::optional<ScopeDeclaration> Module::Search(Hash hash)
{
	//return exportedScope.TryGet(hash);
	// right now we're not building the module so instead search loaded files
	return moduleFile->SearchExports(hash);
}

extern std::mutex hope;

export void RegisterModule(const char* document, const char* moduleName)
{
	auto documentHash = StringHash(document);

	auto scope = new FileScope();
	scope->documentHash = documentHash;

	// this is still not super thread safe because there's no lock around the people who are reading this array.
	hope.lock(); // hope this fixes it!
	scope->fileIndex = (uint16_t)g_fileScopeByIndex.size();
	g_fileScopeByIndex.push_back(scope);
	hope.unlock();

	g_fileScopes.Write(documentHash, scope);

	auto mod = new Module();
	mod->moduleFile = scope;
	mod->moduleFileHash = documentHash;

	auto moduleNameHash = StringHash(moduleName);
	g_modules.Write(moduleNameHash, mod);

}

export long long CreateTreeFromPath(const char* document, const char* moduleName)
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