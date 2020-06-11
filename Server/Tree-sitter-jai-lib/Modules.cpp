#include "TreeSitterJai.h"
#include <vector>
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








/*
static std::vector<std::string_view> BuildModuleScope(Hash document, const char* moduleName)
{
	// so for modules:
	// safe but slow method, treat each  module.jai file as some exported declarations, and then a list of pointers/hashes to each tree that they "load".
	// the trees will always be up to date because they are also the result of any incremental parsing that occurs.
	// we will have to add some export logic to tree, basically sorting or duplicating each declaration to an exported list if it is exported.

	// as for writing module files themselves, we can probalby figure out if we're part of a module by finding out if there is a module.jai file in our current directory
	// if there is, then assume that that file will be loaded into the module.jai, and provide that as context. this is somewhat perilous.
	// the slow part of this is that now we don't calculate a module and lump all their loads together for searching in one big table, we now have to descend into each loaded file, which can be slow.


	// fast method:
	// find out if we're in a module
	// edit module exports without recalculating the whole module
	// does this work? can this work? it will work ok for insertions, but i don't know how to handle deletions.
	// we could check if the module is dirty upon encountering it for the first time, and if it is, then recalculate and compress it to a single table.
	// this maybe doesn't work if the module imports itself... which maybe it can't do unless its namespaced, because otherwise there would be name collisions, or at least they can't export it.
	// we could just re-merge the tables each time its edited, instead of waiting for it to be accessed in a dirty state.
	// the lazy way is probably best.

	// we also need some way of broadcasting module changes so we can trigger reprocessing of files that include it. .... this appears to not be supported for LSP just yet, so multi-file editing will have to wait until re-parsing, unfortunately.
	// im guessing that omnisharp gets around this by using a special vscode extension which is "onDidSemanticTokensChange" or something.





	auto tree = ts_tree_copy(g_trees.Read(document).value());
	auto root = ts_tree_root_node(tree);
	auto buffer = g_buffers.Read(document).value();
	auto moduleHash = StringHash(moduleName);

	auto scopeOpt = g_modules.Read(moduleHash);
	ModuleScope* scope;

	if (!scopeOpt)
	{
		scope = new ModuleScope();
		g_modules.Write(moduleHash, scope);
	}
	else
	{
		scope = scopeOpt.value();
	}




	std::vector<std::string_view> loads;

	auto queryText =
		"(source_file (named_decl (names (identifier) @definition)))"
		"(source_file (named_decl (names (identifier) @func_defn) (function_definition (function_header) @func_defn)))"
		"(source_file (named_decl (names (identifier) @struct_defn) (struct_definition (data_scope) @struct_defn)))"

		"(export_scope_directive) @export"
		"(file_scope_directive) @file"
		"(load_directive name: (string_literal) @load)"
		;

	uint32_t error_offset;
	TSQueryError error_type;

	auto query = ts_query_new(
		g_jaiLang,
		queryText,
		strlen(queryText),
		&error_offset,
		&error_type
	);

	TSQueryCursor* queryCursor = ts_query_cursor_new();
	ts_query_cursor_exec(queryCursor, query, root);

	TSQueryMatch match;
	uint32_t index;

	enum class Capture
	{
		definition,
		func_defn,
		struct_defn,
		exportDirective,
		fileDirective,
		loadDirective,
	};

	bool exporting = true;
	auto code = buffer->GetEntireStringView();
	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		Capture captureType = (Capture)match.captures[index].index;
		auto& node = match.captures[index].node;

		switch (captureType)
		{
		case Capture::definition:
		{
			if (!exporting)
				continue;
			ModuleScopeDeclaration entry;
			entry.definitionNode = node;
			entry.tokenType = GetTokenTypeForNode(node);
			entry.definingFile = document;
#if _DEBUG
			entry.name = GetIdentifierFromBufferCopy(node, buffer);
#endif
			scope->entries[GetIdentifierHash(node, code)] = entry;
			break;
		}
		case Capture::exportDirective:
			exporting = true;
			break;
		case Capture::fileDirective:
			exporting = false;
			break;

		case Capture::func_defn:
		{
			if (!exporting)
				continue;

			ModuleScopeDeclaration entry;
			entry.definitionNode = node;
			entry.tokenType = TokenType::Function;
			entry.definingFile = document;

#if _DEBUG
			entry.name = GetIdentifierFromBufferCopy(node, buffer);
#endif

			TSQueryMatch typeMatch;
			uint32_t nextTypeIndex;
			ts_query_cursor_next_capture(queryCursor, &typeMatch, &nextTypeIndex);
			auto headerNode = typeMatch.captures[nextTypeIndex].node;

			Type* type = new Type();
			type->name = GetIdentifierFromBuffer(headerNode, buffer).CopyMalloc();
			type->definingFile = document;
			entry.type = type;

			scope->entries[GetIdentifierHash(node, code)] = entry;
			break;
		}
		case Capture::struct_defn:
		{
			if (!exporting)
				continue;

			Type* type = new Type(); // leak this 
			type->name = GetIdentifierFromBuffer(node, buffer).CopyMalloc(); // leak this too!
			type->definingFile = document;

			//type->members = 

			ModuleScopeDeclaration entry;
			entry.definitionNode = node;
			entry.tokenType = TokenType::Type;
			entry.definingFile = document;
			entry.type = type;

#if _DEBUG
			entry.name = GetIdentifierFromBufferCopy(node, buffer);
#endif
			scope->entries[GetIdentifierHash(node, code)] = entry;
			break;
		}
		case Capture::loadDirective:
		{
			auto fileName = GetIdentifier(node, code);
			// remove quotes from string literal
			fileName = fileName.substr(1, fileName.size() - 2);
			loads.push_back(fileName);
			break;
		}
		default:
			break;
		}
	}

	ts_tree_delete(tree);
	ts_query_delete(query);
	ts_query_cursor_delete(queryCursor);

	return loads;
}
*/