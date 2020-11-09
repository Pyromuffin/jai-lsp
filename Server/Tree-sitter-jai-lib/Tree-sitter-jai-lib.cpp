// Tree-sitter-jai-lib.cpp : Defines the functions for the static library.
//

#include <assert.h>
#include <tree_sitter/api.h>
#include <vector>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <shared_mutex>

#include "Timer.h"
#include "TreeSitterJai.h"
#include "FileScope.h"

ConcurrentDictionary<TSTree*> g_trees;
ConcurrentDictionary<GapBuffer*> g_buffers;
ConcurrentDictionary<Module*> g_modules;
ConcurrentDictionary<FileScope*> g_fileScopes;
std::vector<const FileScope*> g_fileScopeByIndex;
ConcurrentDictionary<std::string> g_filePaths;

TSLanguage* g_jaiLang;
Constants g_constants;

static char names[1000];
export long long UpdateTree(uint64_t hashValue);


static void SetupBuiltInTypes()
{
	g_constants.builtInTypes[StringHash("bool")] =			TypeHandle{ .fileIndex = UINT16_MAX, .index = 0 };
	g_constants.builtInTypes[StringHash("float32")] =		TypeHandle{ .fileIndex = UINT16_MAX, .index = 1 };
	g_constants.builtInTypes[StringHash("float")] =	  		TypeHandle{ .fileIndex = UINT16_MAX, .index = 1 };
	g_constants.builtInTypes[StringHash("float64")] = 		TypeHandle{ .fileIndex = UINT16_MAX, .index = 2 };
	g_constants.builtInTypes[StringHash("char")] =    		TypeHandle{ .fileIndex = UINT16_MAX, .index = 3 };
	g_constants.builtInTypes[StringHash("string")] =  		TypeHandle{ .fileIndex = UINT16_MAX, .index = 4 };
	g_constants.builtInTypes[StringHash("s8")] =      		TypeHandle{ .fileIndex = UINT16_MAX, .index = 5 };
	g_constants.builtInTypes[StringHash("s16")] =     		TypeHandle{ .fileIndex = UINT16_MAX, .index = 6 };
	g_constants.builtInTypes[StringHash("s32")] =     		TypeHandle{ .fileIndex = UINT16_MAX, .index = 7 };
	g_constants.builtInTypes[StringHash("s64")] =     		TypeHandle{ .fileIndex = UINT16_MAX, .index = 8 };
	g_constants.builtInTypes[StringHash("int")] =	  		TypeHandle{ .fileIndex = UINT16_MAX, .index = 8 };
	g_constants.builtInTypes[StringHash("u8")] =      		TypeHandle{ .fileIndex = UINT16_MAX, .index = 9 };
	g_constants.builtInTypes[StringHash("u16")] =     		TypeHandle{ .fileIndex = UINT16_MAX, .index = 10 };
	g_constants.builtInTypes[StringHash("u32")] =     		TypeHandle{ .fileIndex = UINT16_MAX, .index = 11 };
	g_constants.builtInTypes[StringHash("u64")] =     		TypeHandle{ .fileIndex = UINT16_MAX, .index = 12 };
	g_constants.builtInTypes[StringHash("void")] =    		TypeHandle{ .fileIndex = UINT16_MAX, .index = 13 };

	g_constants.builtInTypesByIndex[0] =  TypeKing{ .name = "bool" };
	g_constants.builtInTypesByIndex[1] = TypeKing{ .name = "float32" };
	g_constants.builtInTypesByIndex[2] = TypeKing{ .name = "float64" };
	g_constants.builtInTypesByIndex[3] = TypeKing{ .name = "char" };
	g_constants.builtInTypesByIndex[4] = TypeKing{ .name = "string" }; // string has some members like data and length.
	g_constants.builtInTypesByIndex[4].members = new Scope();
	g_constants.builtInTypesByIndex[4].members->Add(StringHash("data"), ScopeDeclaration());
	g_constants.builtInTypesByIndex[4].members->Add(StringHash("count"), ScopeDeclaration());

	g_constants.builtInTypesByIndex[5] = TypeKing{ .name = "s8" };
	g_constants.builtInTypesByIndex[6] = TypeKing{ .name = "s16" };
	g_constants.builtInTypesByIndex[7] = TypeKing{ .name = "s32" };
	g_constants.builtInTypesByIndex[8] = TypeKing{ .name = "s64" };
	g_constants.builtInTypesByIndex[9] = TypeKing{ .name = "u8" };
	g_constants.builtInTypesByIndex[10] = TypeKing{ .name = "u16" };
	g_constants.builtInTypesByIndex[11] = TypeKing{ .name = "u32" };
	g_constants.builtInTypesByIndex[12] = TypeKing{ .name = "u64" };
	g_constants.builtInTypesByIndex[13] = TypeKing{ .name = "void" };
}


static void SetupBuiltInFunctions()
{
	// some builtins:
	// size_of
	// type_of
	// assert

}

export int Init()
{
	g_jaiLang = tree_sitter_jai();

	g_constants.constDecl = ts_language_symbol_for_name(g_jaiLang, "const_initializer", strlen("const_initializer"), true);
	g_constants.varDecl = ts_language_symbol_for_name(g_jaiLang, "variable_initializer", strlen("variable_initializer"), true);
	g_constants.import = ts_language_symbol_for_name(g_jaiLang, "import_statement", strlen("import_statement"), true);
	g_constants.funcDefinition = ts_language_symbol_for_name(g_jaiLang, "function_definition", strlen("function_definition"), true);
	g_constants.structDecl = ts_language_symbol_for_name(g_jaiLang, "struct_definition", strlen("struct_definition"), true);
	g_constants.memberAccess = ts_language_symbol_for_name(g_jaiLang, "member_access", strlen("member_access"), true);
	g_constants.memberAccessNothing = ts_language_symbol_for_name(g_jaiLang, "member_access_nothing", strlen("member_access_nothing"), true);
	g_constants.load = ts_language_symbol_for_name(g_jaiLang, "load_statement", strlen("load_statement"), true);
	g_constants.builtInType = ts_language_symbol_for_name(g_jaiLang, "built_in_type", strlen("built_in_type"), true);
	g_constants.identifier = ts_language_symbol_for_name(g_jaiLang, "identifier", strlen("identifier"), true);
	g_constants.namedDecl = ts_language_symbol_for_name(g_jaiLang, "named_decl", strlen("named_decl"), true);
	g_constants.scopeFile = ts_language_symbol_for_name(g_jaiLang, "scope_file", strlen("scope_file"), true);
	g_constants.scopeExport = ts_language_symbol_for_name(g_jaiLang, "scope_export", strlen("scope_export"), true);
	g_constants.dataScope = ts_language_symbol_for_name(g_jaiLang, "data_scope", strlen("data_scope"), true);
	g_constants.imperativeScope = ts_language_symbol_for_name(g_jaiLang, "imperative_scope", strlen("imperative_scope"), true);
	g_constants.parameter = ts_language_symbol_for_name(g_jaiLang, "parameter", strlen("parameter"), true);

	SetupBuiltInTypes();
	SetupBuiltInFunctions();

	return 69420;
}

static inline const char* ReadGapBuffer(void* payload, uint32_t byteOffset, TSPoint position, uint32_t* bytesRead)
{
	GapBuffer* gapBuffer = (GapBuffer*)payload;
	auto afterSize = gapBuffer->after.size();
	auto beforeSize = gapBuffer->before.size();

	if (byteOffset >= beforeSize + afterSize)
	{
		*bytesRead = 0;
		return nullptr;
	}

	if (byteOffset < beforeSize)
	{
		*bytesRead = beforeSize - byteOffset;
		return &gapBuffer->before[byteOffset];
	}
	else
	{
		int index = byteOffset - beforeSize;
		*bytesRead = 1;
		return &gapBuffer->after[afterSize - index - 1];
	}
}



Scope* GetScopeForNode(const TSNode& node, FileScope* scope)
{
	
	auto parent = ts_node_parent(node);
	while (!scope->scopes.contains(parent.id))
	{
		if (ts_node_is_null(parent))
		{
			return nullptr;
		}

		parent = ts_node_parent(parent);
	}

	return &scope->scopes[parent.id];
}

Scope* GetScopeAndParentForNode(const TSNode& node, FileScope* scope, TSNode* outParentNode)
{
	auto parent = ts_node_parent(node);
	while (!scope->scopes.contains(parent.id))
	{
		if (ts_node_is_null(parent))
		{
			return nullptr;
		}

		parent = ts_node_parent(parent);
	}

	*outParentNode = parent;
	return &scope->scopes[parent.id];
}


std::string DebugNode(const TSNode& node, const GapBuffer* gb)
{
	auto name = GetIdentifierFromBufferCopy(node, gb);
	auto symbol = ts_node_type(node);

	return symbol + (": " + name);
}



Scope* GetScopeForNodeDebug(const TSNode& node, FileScope* scope, GapBuffer* gb)
{
	auto parent = ts_node_parent(node);

	while (!scope->scopes.contains(parent.id))
	{
		if (ts_node_is_null(parent))
		{
			return nullptr;
		}

		auto parentName = GetIdentifierFromBufferCopy(parent, gb);
		auto parentSymbol = ts_node_symbol(parent);
		auto symbolName = ts_language_symbol_name(g_jaiLang, parentSymbol);

		parent = ts_node_parent(parent);
	}

	return &scope->scopes[parent.id];
}


static int CountParents(TSNode node)
{
	int parents = 0;
	auto parent = ts_node_parent(node);
	while (!ts_node_is_null(parent))
	{
		parents++;
		parent = ts_node_parent(parent);
	}

	return parents;
}




static SemanticToken GetTokenForNode(TSNode node, DeclarationFlags flags)
{
	auto start = ts_node_start_point(node);
	auto end = ts_node_end_point(node);

	SemanticToken token;
	token.col = start.column;
	token.line = start.row;
	token.length = end.column - start.column;
	token.modifier = (TokenModifier)0;
	token.type = GetTokenTypeFromFlags(flags);
	return token;
}




TokenType GetTokenTypeFromFlags(DeclarationFlags flags)
{
	if (flags & DeclarationFlags::Constant)
		return TokenType::Number;
	if (flags & DeclarationFlags::Function)
		return TokenType::Function;
	if (flags & DeclarationFlags::Struct)
		return TokenType::Type;
	if (flags & DeclarationFlags::Enum)
		return TokenType::Enum;

	return TokenType::Variable;
}


/*
static void HandleUnresolvedReferences(std::vector<int>& unresolvedTokenIndex, std::vector<TSNode>& unresolvedEntry, GapBuffer* buffer, FileScope* fileScope)
{
	for (int i = 0; i < unresolvedTokenIndex.size(); i++)
	{
		// do the slow thing
		auto index = unresolvedTokenIndex[i];
		auto node = unresolvedEntry[i];
		auto identifierHash = GetIdentifierHash(node, buffer);
		TSNode parent;
		auto scope = GetScopeAndParentForNode(node, fileScope, &parent);

		// search scopes going up for entries, if they're data scopes. if they're imperative scopes then declarations have to be in order.
		bool foundInLocalScopes = false;

		while (scope != nullptr)
		{
			if (auto decl = scope->TryGet(identifierHash))
			{
				// if the scope is imperative, then respect ordering, if it is not, just add the entry.
				if (scope->imperative)
				{
					auto definitionStart = decl.value().startByte;
					auto identifierStart = ts_node_start_byte(node);
					if (identifierStart >= definitionStart)
					{
						fileScope->tokens[index].type = GetTokenTypeFromFlags(decl.value().flags);
						foundInLocalScopes = true;
						break;
					}
				}
				else
				{
					fileScope->tokens[index].type = GetTokenTypeFromFlags(decl.value().flags);
					foundInLocalScopes = true;
					break;
				}
			}

			scope = GetScopeAndParentForNode(parent, fileScope, &parent); // aliasing???
		}

		if (foundInLocalScopes)
			continue;

		if (auto decl = fileScope->Search(identifierHash))
		{
			fileScope->tokens[index].type = GetTokenTypeFromFlags(decl.value().flags);
		}
		else
		{
			fileScope->tokens[index].type = (TokenType)-1;
		}
	}
}
*/





export const char* GetSyntaxNice(Hash document)
{
	auto tree = g_trees.Read(document).value();
	auto root = ts_tree_root_node(tree);

	return ts_node_string(root);
}



export const char* GetSyntax(const Hash& document)
{
	auto tree = g_trees.Read(document).value();
	auto root = ts_tree_root_node(tree);

	return ts_node_string(root);
}


export GapBuffer* GetGapBuffer(Hash document)
{
	return g_buffers.Read(document).value();
}

export long long EditTree(uint64_t hashValue, const char* change, int startLine, int startCol, int endLine, int endCol, int contentLength, int rangeLength)
{
	auto timer = Timer("");
	auto documentHash = Hash{ .value = hashValue };

	auto buffer = g_buffers.Read(documentHash).value();
	auto edit = buffer->Edit(startLine, startCol, endLine, endCol, change, contentLength, rangeLength);
	auto tree = g_trees.Read(documentHash).value();
	ts_tree_edit(tree, &edit);
	g_trees.Write(documentHash, tree);

	return timer.GetMicroseconds();
}

static void HandleLoad(Hash documentHash)
{
	auto path = g_filePaths.Read(documentHash).value();

	std::ifstream t(path);
	t.seekg(0, std::ios::end);
	size_t size = t.tellg();
	std::string buffer(size, ' ');
	t.seekg(0);
	t.read(&buffer[0], size);

	CreateTree(path.c_str(), buffer.c_str(), buffer.length());

	// if this has loads handle them too
	auto fileScope = g_fileScopes.Read(documentHash).value();

	for (auto load : fileScope->loads)
	{
		HandleLoad(load);
	}
}


void IncrementalUpdate(TSTree* oldTree, TSTree* newTree)
{
	uint32_t length;
	auto oldRoot = ts_tree_root_node(oldTree);
	auto newRoot = ts_tree_root_node(newTree);

	auto ranges = ts_tree_get_changed_ranges(oldTree, newTree, &length);
	for (int i = 0; i < length; i++)
	{
		auto range = ranges[i];
		auto oldNode = ts_node_named_descendant_for_byte_range(oldRoot, range.start_byte, range.end_byte);
		auto newNode = ts_node_named_descendant_for_byte_range(newRoot, range.start_byte, range.end_byte);

		auto oldStr = ts_node_string(oldNode);
		auto newStr = ts_node_string(newNode);

		std::cout << "old: " << oldStr << "\n" << "new: " << newStr << "\n";
	}
	

	free(ranges);
}



export long long CreateTree(const char* documentPath, const char* code, int length)
{
	auto timer = Timer("");

	auto documentHash = StringHash(documentPath);
	g_filePaths.Write(documentHash, documentPath);

	GapBuffer* buffer;
	if (auto bufferOpt = g_buffers.Read(documentHash))
	{
		buffer = bufferOpt.value();
		*buffer = GapBuffer(code, length);
	}
	else
	{
		buffer = new GapBuffer(code, length);
		g_buffers.Write(documentHash, buffer);
	}
	
	//timings->bufferTime = timer.GetMicroseconds();


	if (auto treeOpt = g_trees.Read(documentHash))
	{
		ts_tree_delete(treeOpt.value());
	}

	TSParser* parser = ts_parser_new();
	ts_parser_set_language(parser, g_jaiLang);

	auto view = buffer->GetEntireStringView();
	auto tree = ts_parser_parse_string(parser, nullptr, view.data(), length);

	//timings->parseTime = timer.GetMicroseconds();

	g_trees.Write(documentHash, tree);
	ts_parser_delete(parser);

	if (auto fileScope = g_fileScopes.Read(documentHash))
	{
		fileScope.value()->Build();
	}
	else
	{
		auto scope = new FileScope();
		scope->fileIndex = g_fileScopeByIndex.size();
		scope->documentHash = documentHash;

		// not thread safe
		g_fileScopeByIndex.push_back(scope);
		g_fileScopes.Write(documentHash, scope);
		scope->Build();
	}
	//timings->scopeTime = timer.GetMicroseconds();
	// handle loads!
	
	auto fileScope = g_fileScopes.Read(documentHash).value();
	
	
	for (auto load : fileScope->loads)
	{
		HandleLoad(load);
	}
	

	return timer.GetMicroseconds();
}



// applies edits!
export long long UpdateTree(uint64_t hashValue)
{
	auto timer = Timer("");
	auto documentHash = Hash{ .value = hashValue };

	auto buffer = g_buffers.Read(documentHash).value();;
	auto tree = g_trees.Read(documentHash).value();

	TSParser* parser = ts_parser_new();
	ts_parser_set_language(parser, g_jaiLang);

	TSInput input;
	input.encoding = TSInputEncodingUTF8;
	input.read = ReadGapBuffer;
	input.payload = buffer;

	auto editedTree = ts_parser_parse(
		parser,
		tree,
		input);


	/*
	// hack to fix incorrect incremental parsing for now!
	editedTree = ts_parser_parse(
		parser,
		editedTree,
		input);
	*/

	g_trees.Write(documentHash, editedTree);

	ts_parser_delete(parser);
	ts_tree_delete(tree);

	auto fileScope = g_fileScopes.Read(documentHash);
	fileScope.value()->Build();

	return timer.GetMicroseconds();
}



export long long GetTokens(uint64_t hashValue, SemanticToken** outTokens, int* count)
{
	auto t = Timer("");
	auto documentHash = Hash{ .value = hashValue };
	auto fileScope = g_fileScopes.Read(documentHash).value();
	*outTokens = fileScope->tokens.data();
	*count = fileScope->tokens.size();

	return t.GetMicroseconds();
}
