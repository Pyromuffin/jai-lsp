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

	auto documentHash = StringHash(document);

	CreateTree(document, buffer.c_str(), buffer.length());
	// take the tree and create a module scope
	auto fileScope = g_fileScopes.Read(documentHash).value();

	auto mod = new Module();
	mod->moduleFile = fileScope;
	mod->moduleFileHash = documentHash;

	auto moduleNameHash = StringHash(moduleName);
	g_modules.Write(moduleNameHash, mod); // don't publish the module before its ready!

	return timer.GetMicroseconds();
}