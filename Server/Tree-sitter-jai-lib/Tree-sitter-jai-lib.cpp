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


import Hashmap;


ConcurrentDictionary<TSTree*> g_trees;
ConcurrentDictionary<GapBuffer*> g_buffers;
ConcurrentDictionary<Module*> g_modules;
ConcurrentDictionary<FileScope*> g_fileScopes;
std::vector<const FileScope*> g_fileScopeByIndex;
ConcurrentDictionary<std::string> g_filePaths;
static std::mutex hope;

TSLanguage* g_jaiLang;
Constants g_constants;

static char names[1000];
export long long UpdateTree(uint64_t hashValue);


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
	g_constants.functionCall = ts_language_symbol_for_name(g_jaiLang, "func_call", strlen("func_call"), true);
	g_constants.argument = ts_language_symbol_for_name(g_jaiLang, "argument", strlen("argument"), true);
	g_constants.unionDecl = ts_language_symbol_for_name(g_jaiLang, "union_definition", strlen("union_definition"), true);
	g_constants.enumDecl = ts_language_symbol_for_name(g_jaiLang, "enum_definition", strlen("enum_definition"), true);
	g_constants.usingStatement = ts_language_symbol_for_name(g_jaiLang, "using_statement", strlen("using_statement"), true);

	//SetupBuiltInTypes();
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


bool GetScopeForNode(const TSNode& node, FileScope* scope, ScopeHandle* handle)
{
	
	auto parent = ts_node_parent(node);
	while (!scope->ContainsScope(parent.id))
	{
		if (ts_node_is_null(parent))
		{
			return false;
		}

		parent = ts_node_parent(parent);
	}

	*handle = scope->GetScopeFromNodeID(parent.id);
	return true;
}

bool GetScopeAndParentForNode(const TSNode& node, FileScope* scope, TSNode* outParentNode, ScopeHandle* handle)
{
	auto parent = ts_node_parent(node);
	while (!scope->ContainsScope(parent.id))
	{
		if (ts_node_is_null(parent))
		{
			return false;
		}

		parent = ts_node_parent(parent);
	}

	*outParentNode = parent;
	*handle = scope->GetScopeFromNodeID(parent.id);
	return true;
}


std::string DebugNode(const TSNode& node, const GapBuffer* gb)
{
	auto name = GetIdentifierFromBufferCopy(node, gb);
	auto symbol = ts_node_type(node);

	return symbol + (": " + name);
}



bool GetScopeForNodeDebug(const TSNode& node, FileScope* scope, GapBuffer* gb, ScopeHandle* handle)
{
	auto parent = ts_node_parent(node);

	while (!scope->ContainsScope(parent.id))
	{
		if (ts_node_is_null(parent))
		{
			return false;
		}

		auto parentName = GetIdentifierFromBufferCopy(parent, gb);
		auto parentSymbol = ts_node_symbol(parent);
		auto symbolName = ts_language_symbol_name(g_jaiLang, parentSymbol);

		parent = ts_node_parent(parent);
	}

	*handle = scope->GetScopeFromNodeID(parent.id);
	return true;
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
	if (flags & DeclarationFlags::BuiltIn)
		return TokenType::EnumMember;

	return TokenType::Variable;
}


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


static std::vector<TSInputEdit> s_edits;

export long long EditTree(uint64_t hashValue, const char* change, int startLine, int startCol, int endLine, int endCol, int contentLength, int rangeLength)
{
	auto timer = Timer("");
	auto documentHash = Hash{ .value = hashValue };

	auto buffer = g_buffers.Read(documentHash).value();
	auto edit = buffer->Edit(startLine, startCol, endLine, endCol, change, contentLength, rangeLength);
	auto tree = g_trees.Read(documentHash).value();

	// this is maybe not thread safe, if we have two edits coming in simultaneously to the same tree.
	ts_tree_edit(tree, &edit);

	s_edits.push_back(edit);
	// g_trees.Write(documentHash, tree); // i don't think we need this. the pointer is not getting modified.

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
		scope->documentHash = documentHash;

		// this is still not super thread safe because there's no lock around the people who are reading this array.
		hope.lock(); // hope this fixes it!
		scope->fileIndex = g_fileScopeByIndex.size();
		g_fileScopeByIndex.push_back(scope);
		hope.unlock();

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



uint64_t GetContext(TSNode node)
{
 //	ts_node_end_byte()
		return 0;
}



void CheckSubtrees(TSNode oldRoot, TSNode newRoot)
{


	auto queryText =
		"(imperative_scope) @imperative.scope"
		"(data_scope) @data.scope"
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
	ts_query_cursor_exec(queryCursor, query, oldRoot);

	TSQueryCursor* newQueryCursor = ts_query_cursor_new();
	ts_query_cursor_exec(newQueryCursor, query, newRoot);

	TSQueryMatch match, newMatch;
	uint32_t index, newIndex;

	enum class ScopeMarker
	{
		imperativeScope,
		dataScope,
	};


	while (ts_query_cursor_next_capture(queryCursor, &match, &index) && ts_query_cursor_next_capture(newQueryCursor, &newMatch, &newIndex))
	{
		ScopeMarker captureType = (ScopeMarker)match.captures[index].index;
		auto node = match.captures[index].node;
		auto newNode = newMatch.captures[newIndex].node;

		if (node.id == newNode.id)
		{
			// party time!
			printf("we did it!");

		}
		else
		{
			printf("we didnt it!");
		}


	}
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

	uint32_t rangeCount;
	auto ranges = ts_tree_get_changed_ranges(tree, editedTree, &rangeCount);
	auto root = ts_tree_root_node(editedTree);

	g_trees.Write(documentHash, editedTree);


	auto fileScope = g_fileScopes.Read(documentHash).value();

	if (rangeCount > 0)
	{
		auto changedNode = ts_node_named_descendant_for_byte_range(root, ranges[0].start_byte, ranges[0].end_byte);
		fileScope->RebuildScope(changedNode, s_edits.data(), s_edits.size());
	}
	else
	{
		auto changedNode = ts_node_named_descendant_for_byte_range(root, s_edits[0].start_byte, s_edits[0].new_end_byte);
		fileScope->RebuildScope(changedNode, s_edits.data(), s_edits.size());
	}
		
	s_edits.clear();


	free(ranges);
	ts_parser_delete(parser);
	ts_tree_delete(tree);

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



/*
	ok so the meta for incremental re-analysis is as follows:
	1) create a hash table of id pointer to scope index, use this to look up scopes when doing tokens and other analysis
		- create this hash table once ! while building the first top level scope, then modify it incrementally. or we could re-create it each time during the token creation phase. then we wouldn't have to look up the ID to delete missing scopes.
		- when encountering a scope index, mark it in the present bit-map that the scope holder keeps track of.
		- at the end if any scopes are not accounted for, append it to the free-list and remove the old IDs from the id -> index hash map.
	2) create an array of byte_offset -> id pointer
		- use this array to find out which scope got modified when looking up a new scope node.
		- you have to keep the edit(s) that caused the new node and apply them to the new array when looking up so that it reflects the new offsets in the file.
		- once you find the old node id, look it up in id -> index hashmap, delete/replace that key with the new id and the recreated scope.
		- then you probably have to go up the stack of parent scopes and do the same for those because of the way tree sitter has to replace every effected node up to the root.






*/