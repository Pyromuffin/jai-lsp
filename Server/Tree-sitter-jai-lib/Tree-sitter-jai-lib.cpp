// Tree-sitter-jai-lib.cpp : Defines the functions for the static library.
//

#include <assert.h>
#include <tree_sitter/api.h>
#include <vector>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "Timer.h"
#include "TreeSitterJai.h"


std::unordered_map<Hash, bool> g_dirty;
std::unordered_map<Hash, TSTree*> g_trees;
std::unordered_map<Hash, GapBuffer> g_buffers;
std::unordered_map<Hash, ModuleScope> g_modules;
std::unordered_map<Hash, FileScope> g_fileScopes;


static TSLanguage* s_jaiLang;
static TSSymbol s_constDecl;
static TSSymbol s_varDecl;
static TSSymbol s_funcDecl;
static TSSymbol s_structDecl;

static char names[1000];
export long long UpdateTree(Hash documentHash);


export int Init()
{
	s_jaiLang = tree_sitter_jai();
	s_constDecl = ts_language_symbol_for_name(s_jaiLang, "constant_definition", strlen("constant_definition"), true);
	s_varDecl = ts_language_symbol_for_name(s_jaiLang, "named_decl", strlen("named_decl"), true);
	s_funcDecl = ts_language_symbol_for_name(s_jaiLang, "named_block_decl", strlen("named_block_decl"), true);
	s_structDecl = ts_language_symbol_for_name(s_jaiLang, "struct_definition", strlen("struct_definition"), true);
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

Hash GetIdentifierHash(const TSNode& node, std::string_view code)
{
	auto start = ts_node_start_byte(node);
	auto end = ts_node_end_byte(node);
	auto length = end - start;
	return StringHash(std::string_view(&code[start], length));
}

Hash GetIdentifierHash(const TSNode& node, GapBuffer* buffer)
{
	auto start = ts_node_start_byte(node);
	auto end = ts_node_end_byte(node);
	return StringHash(buffer_view(start, end, buffer));
}

std::string_view GetIdentifier(const TSNode& node, std::string_view code)
{
	auto start = ts_node_start_byte(node);
	auto end = ts_node_end_byte(node);
	auto length = end - start;
	return std::string_view(&code[start], length);
}


buffer_view GetIdentifierFromBuffer(const TSNode& node, GapBuffer* buffer)
{
	auto start = ts_node_start_byte(node);
	auto end = ts_node_end_byte(node);
	return buffer_view(start, end, buffer);
}


std::string GetIdentifierFromBufferCopy(const TSNode& node, GapBuffer* buffer)
{
	auto start = ts_node_start_byte(node);
	auto end = ts_node_end_byte(node);
	return buffer_view(start, end, buffer).Copy();
}

TokenType GetTokenTypeForNode(const TSNode& node)
{
	auto symbol = ts_node_symbol(node);

	if (symbol == s_constDecl)
		return TokenType::EnumMember;
		
	if (symbol == s_varDecl)
		return TokenType::Variable;

	if (symbol == s_funcDecl)
		return TokenType::Function;

	if (symbol == s_structDecl)
		return TokenType::Struct;

	return TokenType::Comment;
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


std::string DebugNode(const TSNode& node, GapBuffer* gb)
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
		auto symbolName = ts_language_symbol_name(s_jaiLang, parentSymbol);

		parent = ts_node_parent(parent);
	}

	return &scope->scopes[parent.id];
}

static std::vector<std::string_view> BuildModuleScope(Hash document, const char* moduleName)
{
	auto tree = ts_tree_copy(g_trees[document]);
	auto root = ts_tree_root_node(tree);
	auto buffer = &g_buffers[document];

	ModuleScope& scope = g_modules[StringHash(moduleName)];
	std::vector<std::string_view> loads;

	auto queryText =
		//"(source_file (named_decl name: (identifier) @definition))"
		//"(source_file (named_block_decl name: (identifier) @func_defn (function_definition)))"
		//"(source_file (named_block_decl name: (identifier) @struct_defn (struct_definition)))"
		
		"(source_file (named_decl (names (identifier) @definition)))"
		//"(source_file (named_decl (names (identifier) @definition) \":\" \":\"))"
		"(source_file (named_block_decl (names (identifier) @func_defn) (function_definition)))"
		"(source_file (named_block_decl (names (identifier) @struct_defn) (struct_definition)))"
		//"(source_file (named_block_decl (names (identifier) @enum_defn) (enum_definition)))"
		
		
		"(export_scope_directive) @export"
		"(file_scope_directive) @file"
		"(load_directive name: (string_literal) @load)"
		;

	uint32_t error_offset;
	TSQueryError error_type;

	auto query = ts_query_new(
		s_jaiLang,
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
			ModuleScopeEntry entry;
			entry.definitionNode = node;
			entry.type = GetTokenTypeForNode(node);
			entry.definingFile = document;
#if _DEBUG
			entry.name = GetIdentifierFromBufferCopy(node, buffer);
#endif
			scope.entries[GetIdentifierHash(node, code)] = entry;
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
			ModuleScopeEntry entry;
			entry.definitionNode = node;
			entry.type = TokenType::Function;
			entry.definingFile = document;

#if _DEBUG
			entry.name = GetIdentifierFromBufferCopy(node, buffer);
#endif
			scope.entries[GetIdentifierHash(node, code)] = entry;
			break;
		}
		case Capture::struct_defn:
		{
			if (!exporting)
				continue;
			ModuleScopeEntry entry;
			entry.definitionNode = node;
			entry.type = TokenType::Type;
			entry.definingFile = document;

#if _DEBUG
			entry.name = GetIdentifierFromBufferCopy(node, buffer);
#endif
			scope.entries[GetIdentifierHash(node, code)] = entry;
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

static ScopeEntry AddEntryToScope(TSNode node, TokenType type, GapBuffer* buffer, Scope* scope)
{
	ScopeEntry entry;
	entry.definitionNode = node;
	entry.type = type;
#if _DEBUG
	entry.name = GetIdentifierFromBufferCopy(node, buffer);
#endif

	scope->entries[GetIdentifierHash(node, buffer)] = entry;
	return entry;
}

static ScopeEntry AddEntry(TSNode node, TokenType type, FileScope* fileScope, GapBuffer* buffer)
{
	Scope* scope = GetScopeForNode(node, fileScope);
	if (scope == nullptr)
	{
		scope = &fileScope->file;
	}

	auto start = ts_node_start_point(node);
	auto end = ts_node_end_point(node);

	SemanticToken token;
	token.col = start.column;
	token.line = start.row;
	token.length = end.column - start.column;
	token.modifier = (TokenModifier)0;
	token.type = type;
	fileScope->tokens.push_back(token);

	return AddEntryToScope(node, type, buffer, scope);
}

static void BuildFileScope(Hash documentHash)
{
	// ideally we do this breadth first per scope, but that seems like a bit of a hassle at the moment. 
	// because of this, it will fail to properly highlight elements in data scopes that are out of order, and are also not file scope. 

	FileScope* fileScope = &g_fileScopes[documentHash];
	fileScope->imports.clear();
	fileScope->scopes.clear();
	fileScope->tokens.clear();
	fileScope->file.entries.clear();

	auto buffer = &g_buffers[documentHash];

	auto tree = ts_tree_copy(g_trees[documentHash]);
	auto root = ts_tree_root_node(tree);
	

	auto queryText =
		
		"(source_file) @file.scope"
		"(block) @local.scope"
		"(for_loop) @local.scope"

		"(named_decl (names (identifier) @const_decl) \":\" \":\")"
		"(named_decl (names (identifier) @var_decl))"
		"(named_block_decl (names (identifier) @func_defn) (function_definition))"
		"(named_block_decl (names (identifier) @struct_defn) (struct_definition))"
		"(named_block_decl (names (identifier) @enum_defn) (enum_definition))"
		"(parameter name: (identifier) @parameter_decl)"
		"(identifier) @var_ref"
		"(block_end) @block_end"
		"(import_statement name: (string_literal) @import)"
		;

	uint32_t error_offset;
	TSQueryError error_type;

	auto query = ts_query_new(
		s_jaiLang,
		queryText,
		strlen(queryText),
		&error_offset,
		&error_type
	);

	TSQueryCursor* queryCursor = ts_query_cursor_new();
	ts_query_cursor_exec(queryCursor, query, root);

	TSQueryMatch match;
	uint32_t index;

	enum class ScopeMarker
	{
		fileScope,
		localScope,
		const_decl,
		var_decl,
		function_defn,
		struct_defn,
		enum_defn,
		parmeter_decl,
		var_ref,
		block_end,
		import,
	};

	std::vector<TSNode> parameters;
	std::vector<Scope*> scopeKing;
	std::vector<TSNode> unresolvedEntry;
	std::vector<int> unresolvedTokenIndex;

	bool skipNextIdentifier = false;
	bool skipNextVarDecl = false;
	bool scopeIsImperative = false;

	// source file, struct, enum, union scopes are data scopes.

	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		ScopeMarker captureType = (ScopeMarker)match.captures[index].index;
		auto node = match.captures[index].node;
		switch (captureType)
		{
		case ScopeMarker::localScope:
		{
			auto localScope = &fileScope->scopes[node.id];
			localScope->imperative = scopeIsImperative;
			auto start = ts_node_start_byte(node);
			auto end = ts_node_end_byte(node);
#if _DEBUG
			localScope->content = buffer_view(start, end, buffer);
#endif
			// if we have any paremeters, inject them here.
			for (int i = 0; i < parameters.size(); i++)
			{
				AddEntryToScope(parameters[i], TokenType::Variable, buffer, localScope);
			}
			parameters.clear();
			
			scopeKing.push_back(localScope);
			break;
		}
		case ScopeMarker::fileScope:
		{
			auto bigScope = &fileScope->file;
			auto start = ts_node_start_byte(node);
			auto end = ts_node_end_byte(node);
#if _DEBUG
			bigScope->content = buffer_view(start, end, buffer);
#endif
			bigScope->imperative = false;
			scopeKing.push_back(bigScope);
			break;
		}
		case ScopeMarker::var_decl:
		{
			if (!skipNextVarDecl)
			{
				skipNextIdentifier = true;
				AddEntry(node, TokenType::Variable, fileScope, buffer);
			}
			skipNextVarDecl = false;
			break;
		}
		
		case ScopeMarker::const_decl:
		{
			skipNextIdentifier = true;
			skipNextVarDecl = true; // this is complicated and kind of dumb.
			AddEntry(node, TokenType::Number, fileScope, buffer);
			break;
		}
		
		case ScopeMarker::function_defn:
		{
			skipNextIdentifier = true;
			scopeIsImperative = true;
			AddEntry(node, TokenType::Function, fileScope, buffer);
			break;
		}
		
		case ScopeMarker::struct_defn:
		{
			skipNextIdentifier = true;
			scopeIsImperative = false;
			AddEntry(node, TokenType::Type, fileScope, buffer);
			break;
		}
		case ScopeMarker::enum_defn:
		{
			skipNextIdentifier = true;
			scopeIsImperative = false;
			AddEntry(node, TokenType::Enum, fileScope, buffer);
			break;
		}
		
		
		case ScopeMarker::parmeter_decl:
		{
			skipNextIdentifier = true;

			auto start = ts_node_start_point(node);
			auto end = ts_node_end_point(node);
			SemanticToken token;
			token.col = start.column;
			token.line = start.row;
			token.length = end.column - start.column;
			token.modifier = (TokenModifier)0;
			token.type = TokenType::Variable;
			fileScope->tokens.push_back(token);

			parameters.push_back(node);
			break;
		}
		
		case ScopeMarker::var_ref:
		{
			if (!skipNextIdentifier)
			{
				auto hash = GetIdentifierHash(node, buffer);

				auto start = ts_node_start_point(node);
				auto end = ts_node_end_point(node);
				SemanticToken token;
				token.col = start.column;
				token.line = start.row;
				token.length = end.column - start.column;
				token.modifier = (TokenModifier)0;

				// this means we are looking at a variable reference, and not a declaration.
				// look up value in hash tables:
				for (int sp = 0; sp < scopeKing.size(); sp++)
				{
					int index = scopeKing.size() - sp - 1;
					auto frame = scopeKing[index];
					if (frame->entries.contains(hash))
					{
						token.type = frame->entries[hash].type;
						goto done;
					}
				}

				// search modules
				for (auto moduleHash : fileScope->imports)
				{
					auto moduleScope = &g_modules[moduleHash];
					if( moduleScope->entries.contains(hash) )
					{
						token.type = moduleScope->entries[hash].type;
						goto done;
					}
				}

				// if we get here then we've got an unresolved identifier, that we will check when we've parsed the entire document.
				unresolvedEntry.push_back(node);
				unresolvedTokenIndex.push_back(fileScope->tokens.size());

				done:
				fileScope->tokens.push_back(token);
			}
			
			skipNextIdentifier = false;
			break;
		}
		case ScopeMarker::block_end:
		{
			scopeKing.pop_back();
			scopeIsImperative = scopeKing.back()->imperative;
			break;
		}
		case ScopeMarker::import:
		{
			// elide the quotation marks
			auto startOffset = ts_node_start_byte(node) + 1;
			auto endOffset = ts_node_end_byte(node) - 1;
			buffer_view view = buffer_view(startOffset, endOffset, buffer);
			auto moduleNameHash = StringHash(view);
			fileScope->imports.push_back(moduleNameHash);
		}
		break;

		default:
			break;
		}
	}

	// handle unresolved nodes
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
			if (scope->entries.contains(identifierHash))
			{
				// if the scope is imperative, then respect ordering, if it is not, just add the entry.
				if (scope->imperative)
				{
					auto definition = scope->entries[identifierHash].definitionNode;
					auto definitionStart = ts_node_start_byte(definition);
					auto identifierStart = ts_node_start_byte(node);
					if (identifierStart >= definitionStart)
					{
						auto entry = scope->entries[identifierHash];
						fileScope->tokens[index].type = entry.type;
						foundInLocalScopes = true;
						break;
					}
				}
				else
				{
					auto entry = scope->entries[identifierHash];
					fileScope->tokens[index].type = entry.type;
					foundInLocalScopes = true;
					break;
				}
			}

			scope = GetScopeAndParentForNode(parent, fileScope, &parent); // aliasing???
		}

		if (foundInLocalScopes)
			continue;

		if(fileScope->file.entries.contains(identifierHash))
		{
			auto entry = fileScope->file.entries[identifierHash];
			fileScope->tokens[index].type = entry.type;
		}
		else
		{
			// search modules
			bool found = false;
			for (auto moduleHash : fileScope->imports)
			{
				auto moduleScope = &g_modules[moduleHash];
				if (moduleScope->entries.contains(identifierHash))
				{
					auto entry = moduleScope->entries[identifierHash];
					fileScope->tokens[index].type = entry.type;
					found = true;
					break;
				}
			}

			if (!found)
				fileScope->tokens[index].type = (TokenType)-1;
		}
	}



	ts_tree_delete(tree);
	ts_query_delete(query);
	ts_query_cursor_delete(queryCursor);
}



export const char* GetSyntaxNice(Hash document)
{
	auto tree = g_trees[document];
	auto root = ts_tree_root_node(tree);

	return ts_node_string(root);
}



export const char* GetSyntax(const Hash& document)
{
	auto tree = g_trees[document];
	auto root = ts_tree_root_node(tree);

	return ts_node_string(root);
}


export GapBuffer* GetGapBuffer(Hash document)
{
	return &g_buffers[document];
}

export long long EditTree(Hash documentHash, const char* change, int startLine, int startCol, int endLine, int endCol, int contentLength, int rangeLength)
{
	auto timer = Timer("");

	assert(g_trees.contains(documentHash));
	assert(g_buffers.contains(documentHash));

	auto buffer = &g_buffers[documentHash];
	auto edit = buffer->Edit(startLine, startCol, endLine, endCol, change, contentLength, rangeLength);
	auto tree = g_trees[documentHash];
	ts_tree_edit(tree, &edit);
	g_trees[documentHash] = tree;
	g_dirty[documentHash] = true;

	return timer.GetMicroseconds();
}


static void HandleImports(Hash documentHash)
{
	auto buffer = &g_buffers[documentHash];
	auto tree = ts_tree_copy(g_trees[documentHash]);
	auto root = ts_tree_root_node(tree);

	uint32_t error_offset;
	TSQueryError error_type;
	auto queryText =
		"(import_directive name: (string_literal) @import)"
		//"(load_statement name: (string_literal) @load)"
		;

	auto var_decl_query = ts_query_new(
		s_jaiLang,
		queryText,
		strlen(queryText),
		&error_offset,
		&error_type
	);

	TSQueryCursor* queryCursor = ts_query_cursor_new();
	ts_query_cursor_exec(queryCursor, var_decl_query, root);

	TSQueryMatch match;
	uint32_t index;
	int cursor = 0;
	
	auto scope = &g_fileScopes[documentHash];
	scope->imports.clear();

	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		auto node = match.captures[index].node;
		auto captureIndex = match.captures[index].index;

		// elide the quotation marks
		auto startOffset = ts_node_start_byte(node) + 1;
		auto endOffset = ts_node_end_byte(node) - 1;
		buffer_view view = buffer_view(startOffset, endOffset, buffer);

		auto moduleNameHash = StringHash(view);
		scope->imports.push_back(moduleNameHash);
	}
	
	ts_query_cursor_delete(queryCursor);
	ts_query_delete(var_decl_query);
}


export long long CreateTree(Hash documentHash, const char* code, int length)
{
	auto timer = Timer("");
	g_buffers[documentHash] = GapBuffer(code, length);

	if (g_trees.contains(documentHash))
	{
		ts_tree_delete(g_trees[documentHash]);
	}

	TSParser* parser = ts_parser_new();
	ts_parser_set_language(parser, s_jaiLang);

	auto view = g_buffers[documentHash].GetEntireStringView();
	auto tree = ts_parser_parse_string(parser, nullptr, view.data(), length);

	g_trees[documentHash] = tree;
	g_dirty[documentHash] = false;

	ts_parser_delete(parser);


	BuildFileScope(documentHash);

	return timer.GetMicroseconds();
}

static void HandleLoad(std::string_view loadName, const char* moduleDirectory, const char* moduleName)
{
	// chop off moduleDirectory "module.jai" and insert loadname
	auto path = std::filesystem::path(moduleDirectory);
	path.replace_filename(loadName);

	std::ifstream t(path);
	t.seekg(0, std::ios::end);
	size_t size = t.tellg();
	std::string buffer(size, ' ');
	t.seekg(0);
	t.read(&buffer[0], size);

	auto pathStringHash = StringHash(path.string());

	CreateTree(pathStringHash, buffer.c_str(), buffer.length());

	BuildModuleScope(pathStringHash, moduleName);
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

	CreateTree(documentHash, buffer.c_str(), buffer.length());
	// take the tree and create a module scope

	auto loads = BuildModuleScope(documentHash, moduleName);

	for (auto load : loads)
	{
		HandleLoad(load, document, moduleName);
	}

	return timer.GetMicroseconds();
}

// applies edits!
export long long UpdateTree(Hash documentHash)
{
	auto timer = Timer("");

	assert(g_trees.contains(documentHash));
	assert(g_buffers.contains(documentHash));

	if (!g_dirty[documentHash])
		return 0;

	auto buffer = &g_buffers[documentHash];
	auto tree = g_trees[documentHash];

	TSParser* parser = ts_parser_new();
	ts_parser_set_language(parser, s_jaiLang);

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

	g_trees[documentHash] = editedTree;

	ts_parser_delete(parser);
	ts_tree_delete(tree);

	BuildFileScope(documentHash);

	g_dirty[documentHash] = false;

	return timer.GetMicroseconds();
}

export const char* GetCompletionItems(const char* code, int row, int col)
{
	/*
	TSParser* parser = ts_parser_new();
	auto jaiLang = tree_sitter_jai();

	ts_parser_set_language(parser, tree_sitter_jai());

	auto tree = ts_parser_parse_string(
		parser,
		NULL,
		code,
		strlen(code));
	TSNode root_node = ts_tree_root_node(tree);

	auto scopeMap = BuildUpScopes(root_node, jaiLang, code);
	auto fileScope = GetScopeForNode(root_node, scopeMap);

	TSPoint start;
	start.column = col;
	start.row = row;
	auto end = start;

	const auto& currentNode = ts_node_named_descendant_for_point_range(root_node, start, end);
	Scope* scope;
	
	if (scopeMap.contains(currentNode.id))
	{
		scope = &scopeMap[currentNode.id];
	}
	else
	{
		scope = GetScopeForNode(currentNode, scopeMap);
	}
	
	int cursor = 0;
	while (scope != nullptr)
	{
		for (auto& entry : scope->entries)
		{
			if (entry.position.row < row || (entry.position.row == row && entry.position.column < col) || scope == fileScope)
			{
				strncpy_s(&names[cursor], sizeof(names) - cursor, entry.name.data(), entry.name.length());
				cursor += entry.name.length();
				names[cursor] = ',';
				cursor++;
			}
		}
		scope = GetScopeForNode(scope->node, scopeMap);
	}

	cursor--;
	if (cursor < 0) cursor = 0;
	names[cursor] = '\0';
	
	ts_tree_delete(tree);
	ts_parser_delete(parser);
		
	return names;
	*/
	return nullptr;
}


export long long GetTokens(Hash documentHash, SemanticToken** outTokens, int* count)
{
	auto t = Timer("");
	auto fileScope = &g_fileScopes[documentHash];
	*outTokens = fileScope->tokens.data();
	*count = fileScope->tokens.size();

	return t.GetMicroseconds();
	/*
	assert(g_trees.contains(documentHash));
	assert(g_buffers.contains(documentHash));

	s_tokens.clear();

	auto totalTimer = Timer("total time");

	auto tree = ts_tree_copy(g_trees[documentHash]);
	auto buffer = &g_buffers[documentHash];
	
	TSNode root_node = ts_tree_root_node(tree);

	AddVaraibleReferenceTokens(fileScope, root_node, buffer, s_tokens);

	*outTokens = s_tokens.data();
	*count = s_tokens.size();

	ts_tree_delete(tree);
	return totalTimer.GetMicroseconds();
	*/
}
