// Tree-sitter-jai-lib.cpp : Defines the functions for the static library.
//

#include <assert.h>
#include <tree_sitter/api.h>

#include <vector>
#include <unordered_map>
#include <string_view>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "Timer.h"
#include "GapBuffer.h"


#define export extern "C" __declspec(dllexport)

enum class TokenType
{
	Documentation,
	Comment,
	Keyword,
	String,
	Number,
	Regexp,
	Operator,
	Namespace,
	Type,
	Struct,
	Class,
	Interface,
	Enum,
	TypeParameter,
	Function,
	Member,
	Property,
	Macro,
	Variable,
	Parameter,
	Label,
	EnumMember,
};

enum class TokenModifier
{
	Documentation = 1 << 0,
	Declaration = 1 << 1,
	Definition = 1 << 2,
	Static = 1 << 3,
	Abstract = 1 << 4,
	Deprecated = 1 << 5,
	Readonly = 1 << 6,
};

struct SemanticToken
{
	int line;
	int col;
	int length;
	TokenType type;
	TokenModifier modifier;
};

struct ScopeEntry
{
	TSPoint position;
	TokenType type;
};


struct Scope
{
	std::unordered_map<buffer_view, ScopeEntry> entries;
	buffer_view content;
	TSNode node; // these need to be updated when edited???
};

struct ModuleScope
{
	std::unordered_map<std::string_view, ScopeEntry> entries;
};

extern "C" TSLanguage* tree_sitter_jai();
static TSLanguage* s_jaiLang;

static char names[1000];

static TSSymbol s_constDecl;
static TSSymbol s_varDecl;
static TSSymbol s_funcDecl;
static TSSymbol s_structDecl;
static std::unordered_map<std::string_view, TSTree*> s_trees;
static std::unordered_map<std::string_view, GapBuffer> s_buffers;
static std::unordered_map<std::string_view, char*> s_names;
static std::unordered_map<std::string_view, ModuleScope> s_modules;
typedef std::unordered_map<const void*, Scope> ScopeMap;


export int Init()
{
	s_jaiLang = tree_sitter_jai();
	s_constDecl = ts_language_symbol_for_name(s_jaiLang, "constant_definition", strlen("constant_definition"), true);
	s_varDecl = ts_language_symbol_for_name(s_jaiLang, "variable_decl", strlen("variable_decl"), true);
	s_funcDecl = ts_language_symbol_for_name(s_jaiLang, "function_definition", strlen("function_definition"), true);
	s_structDecl = ts_language_symbol_for_name(s_jaiLang, "struct_definition", strlen("struct_definition"), true);
	//nameId = ts_language_field_id_for_name(jaiLang, "name", strlen("name"));
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

std::string_view GetIdentifier(const TSNode& node, std::string_view code)
{
	auto start = ts_node_start_byte(node);
	auto end = ts_node_end_byte(node);
	auto length = end - start;
	return std::string_view(&code[start], length);
}


static std::unordered_map<buffer_view, TokenType> s_tokenTypes;


buffer_view GetIdentifier(const TSNode& node, GapBuffer* buffer)
{
	auto start = ts_node_start_byte(node);
	auto end = ts_node_end_byte(node);
	auto length = end - start;
	return { .buffer = buffer, .start = start, .length = length };
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

Scope* GetScopeForNode(const TSNode& node, ScopeMap& scopeMap)
{
	auto parent = ts_node_parent(node);
	while (!scopeMap.contains(parent.id))
	{
		if (ts_node_is_null(parent))
		{
			return nullptr;
		}

		parent = ts_node_parent(parent);
	}

	return &scopeMap[parent.id];
}

std::vector<std::string_view> BuildModuleScope(const char* document, const char* moduleName)
{
	auto tree = ts_tree_copy(s_trees[document]);
	auto root = ts_tree_root_node(tree);
	auto buffer = &s_buffers[document];

	ModuleScope& scope = s_modules[moduleName];
	std::vector<std::string_view> loads;

	auto queryText =
		"(source_file (variable_decl name : (identifier) @definition))"
		"(source_file (compound_variable_decl name : (identifier) @definition))"
		"(source_file (constant_definition name : (identifier) @definition_parent))"
		"(source_file (struct_definition name : (identifier) @definition_parent))"
		"(source_file (function_definition name : (identifier) @definition_parent))"
		//"(enum_definition name : (identifier) @definition)"
		"(export_scope_directive) @export"
		"(file_scope_directive) @file"
		"(load_directive name : (string_literal) @load)"
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
		definition_parent,
		exportDirective,
		fileDirective,
		loadDirective,
	};

	bool exporting = false;
	auto code = buffer->GetEntireStringView();
	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		Capture captureType = (Capture)match.captures[index].index;
		auto& node = match.captures[index].node;

		switch (captureType)
		{
		case Capture::definition:
			if (!exporting)
				continue;
			ScopeEntry entry;
			entry.position = ts_node_start_point(node);
			entry.type = GetTokenTypeForNode(node);
			scope.entries[GetIdentifier(node, code)] = entry;
			break;
		case Capture::exportDirective:
			exporting = true;
			break;
		case Capture::fileDirective:
			exporting = false;
			break;
		case Capture::definition_parent:
			entry.position = ts_node_start_point(node);
			entry.type = GetTokenTypeForNode(ts_node_parent(node));
			scope.entries[GetIdentifier(node, code)] = entry;
			break;
		case Capture::loadDirective:
			{
				auto fileName = GetIdentifier(node, code);
				// remove quotes from string literal
				fileName = fileName.substr(1, fileName.size() - 2);
				loads.push_back(fileName);
			}
			break;
		default:
			break;
		}
	}

	ts_tree_delete(tree);
	ts_query_delete(query);
	ts_query_cursor_delete(queryCursor);

	return loads;
}

/*

ScopeMap BuildUpScopes(const TSNode& root, const TSLanguage* lang, const char* code)
{
	// a scope has a start, and end position
	Scope fileScope;
	fileScope.content = std::string_view(code, ts_node_end_byte(root));
	fileScope.node = root;

	ScopeMap scopeMap;
	scopeMap[root.id] = fileScope;

	auto queryText =
		"(block) @local.scope"
		"(source_file) @file.scope"
		"(function_definition) @local.scope"
		"(struct_definition) @local.scope"
		"(for_loop) @local.scope"

		"(variable_decl name : (identifier) @local.definition)"
		"(compound_variable_decl name : (identifier) @local.definition)"
		"(named_parameter_decl name : (identifier) @local.definition)"
		"(for_loop name : (identifier) @local.definition)"
		"(for_loop names : (identifier) @local.definition)"
		"(constant_definition name : (identifier) @local.definition)"
		"(struct_definition name : (identifier) @export.definition)"
		"(function_definition name : (identifier) @export.definition)";
		//"(identifier) @local.reference";

	uint32_t error_offset;
	TSQueryError error_type;

	auto query = ts_query_new(
		lang,
		queryText,
		strlen(queryText),
		&error_offset,
		&error_type
	);

	TSQueryCursor* queryCursor = ts_query_cursor_new();
	ts_query_cursor_exec(queryCursor, query, root);

	TSQueryMatch match;
	uint32_t index;
	int cursor = 0;

	enum class ScopeMarker
	{
		localScope,
		fileScope,
		localDefinition,
		exportDefn,
	};

	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		ScopeMarker captureType = (ScopeMarker)match.captures[index].index;
		auto& node = match.captures[index].node;
		switch (captureType)
		{
		case ScopeMarker::localScope:
		{	
			Scope localScope;
			auto start = ts_node_start_byte(node);
			auto length = ts_node_end_byte(node) - start;
			localScope.content = std::string_view(&code[start], length);
			localScope.node = node;
			scopeMap[node.id] = localScope;
			break;
		}
		case ScopeMarker::fileScope:
			break;
		case ScopeMarker::localDefinition:
		{
			Scope* scope = GetScopeForNode(node, scopeMap);
			ScopeEntry entry;
			entry.name = GetIdentifier(node, code);
			entry.position = ts_node_start_point(node);
			entry.type = GetTokenTypeForNode(node);
			scope->entries.push_back(entry);
			break;
		}
		case ScopeMarker::exportDefn:
		{
			// function definitions, and struct definitions, while syntactically outside of the block, they are included in the defintion scope for parsing reasons.
			// so to make their names available we export their names to the surrounding scope.
			Scope* scope = GetScopeForNode(node, scopeMap);
			scope = GetScopeForNode(scope->node, scopeMap);
			ScopeEntry entry;
			entry.name = GetIdentifier(node, code);
			entry.position = ts_node_start_point(node);
			entry.type = GetTokenTypeForNode(node);
			scope->entries.push_back(entry);
			break;
		}
		default:
			break;
		}
	}

	ts_query_delete(query);
	ts_query_cursor_delete(queryCursor);

	return scopeMap;
}
*/

export const char* GetSyntax(const char* document)
{
	auto tree = s_trees[document];
	auto root = ts_tree_root_node(tree);

	return ts_node_string(root);
}

export GapBuffer* GetGapBuffer(const char* document)
{
	return &s_buffers[document];
}

enum class ChangeType
{
	Insert,
	Delete,
	Replace,
};

export long long EditTree(const char* document, const char* change, int startLine, int startCol, int endLine, int endCol, int contentLength, int rangeLength)
{
	auto timer = Timer("");

	assert(s_trees.contains(document));
	assert(s_buffers.contains(document));
	assert(s_names.contains(document));

	auto buffer = &s_buffers[document];
	auto edit = buffer->Edit(startLine, startCol, endLine, endCol, change, contentLength, rangeLength);
	auto tree = s_trees[document];
	ts_tree_edit(tree, &edit);

	return timer.GetMicroseconds();
}


export long long UpdateTree(const char* document)
{
	auto timer = Timer("");

	assert(s_trees.contains(document));
	assert(s_buffers.contains(document));
	assert(s_names.contains(document));
	
	auto buffer = &s_buffers[document];
	auto tree = s_trees[document];

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

	s_trees[document] = editedTree;

	ts_parser_delete(parser);
	ts_tree_delete(tree);

	return timer.GetMicroseconds();
}

static void HandleImports(const char* document)
{
	const char* modulesDir = "C:\\Users\\pyrom\\Desktop\\jai\\modules\\";

	auto buffer = &s_buffers[document];
	auto tree = ts_tree_copy(s_trees[document]);
	auto root = ts_tree_root_node(tree);

	uint32_t error_offset;
	TSQueryError error_type;
	auto queryText =
		"(import_directive name: (string_literal) @import)"
		"(load_statement name: (string_literal) @load)"
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
	
	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		auto& node = match.captures[index].node;
		auto captureIndex = match.captures[index].index;

		auto moduleName = GetIdentifier(node, buffer);
		auto path = std::string(modulesDir);
		//path += moduleName.Copy().substr(1, moduleName.length -1); // remove the quotes
		auto moduleFiles = std::filesystem::directory_iterator(path);
		
		for (auto file : moduleFiles)
		{
			auto filepath = file.path();
			if (filepath == "module.jai")
			{
				auto stream = std::ifstream(filepath);
			}
		}

	}
	
	ts_query_cursor_delete(queryCursor);
	ts_query_delete(var_decl_query);
}


export long long CreateTree(const char* document, const char* code, int length)
{
	auto timer = Timer("");

	auto nameLength = strlen(document) + 1;
	auto name = (char*)malloc(nameLength);
	strcpy_s(name, nameLength,  document);

	s_names[name] = name; // leak!
	s_buffers[name] = GapBuffer(code, length);

	if (s_trees.contains(name))
	{
		ts_tree_delete(s_trees[name]);
	}

	TSParser* parser = ts_parser_new();
	ts_parser_set_language(parser, s_jaiLang);

	auto view = s_buffers[name].GetEntireStringView();
	auto tree = ts_parser_parse_string(parser, nullptr, view.data(), length);

	s_trees[name] = tree;
	ts_parser_delete(parser);

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

	auto pathString = path.string();

	CreateTree(pathString.c_str(), buffer.c_str(), buffer.length());

	BuildModuleScope(pathString.c_str(), moduleName);
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

	CreateTree(document, buffer.c_str(), buffer.length());
	// take the tree and create a module scope

	auto nameLength = strlen(moduleName) + 1;
	auto name = (char*)malloc(nameLength); // leak
	strcpy_s(name, nameLength, moduleName);

	auto loads = BuildModuleScope(document, name);

	for (auto load : loads)
	{
		HandleLoad(load, document, name);

	}

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


static void AddVaraibleReferenceTokens(const char* code, int row, int col,  TSNode root_node, std::vector<SemanticToken>& tokens, const std::unordered_map<std::string_view, TokenType>& declMap, GapBuffer* gb)
{
	uint32_t error_offset;
	TSQueryError error_type;
	auto queryText =
		"(identifier) @var_ref";

	auto var_decl_query = ts_query_new(
		s_jaiLang,
		queryText,
		strlen(queryText),
		&error_offset,
		&error_type
	);

	TSQueryCursor* queryCursor = ts_query_cursor_new();
	ts_query_cursor_exec(queryCursor, var_decl_query, root_node);

	TSQueryMatch match;
	uint32_t index;
	int cursor = 0;

	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		auto& node = match.captures[index].node;
		auto captureIndex = match.captures[index].index;
		auto start = ts_node_start_point(node);
		auto end = ts_node_end_point(node);

		SemanticToken token;
		token.col = start.column;
		token.line = start.row;
		token.length = end.column - start.column;
		token.modifier = (TokenModifier)0;

		auto identifier = GetIdentifier(node, code);
		auto search = declMap.find(identifier);
		if (search != declMap.end())
		{
			token.type = search->second;
			tokens.push_back(token);
		}
		else
		{
			for (auto& mod : s_modules)
			{
				if (mod.second.entries.contains(identifier))
				{
					token.type = mod.second.entries[identifier].type;
					tokens.push_back(token);
					break;
				}
			}
		}

	}

	ts_query_cursor_delete(queryCursor);
	ts_query_delete(var_decl_query);
}


export long long GetTokens(const char* document, SemanticToken** outTokens, int* count)
{
	static std::vector<SemanticToken> s_tokens;
	std::unordered_map<std::string_view, TokenType> declMap;

	assert(s_trees.contains(document));
	assert(s_buffers.contains(document));
	assert(s_names.contains(document));

	s_tokens.clear();

	auto totalTimer = Timer("total time");

	auto tree = ts_tree_copy(s_trees[document]);
	auto buffer = &s_buffers[document];
	auto code = s_buffers[document].Copy();
	TSNode root_node = ts_tree_root_node(tree);
	uint32_t error_offset;
	TSQueryError error_type;

	enum class DeclType
	{
		variable,
		function,
		constant,
		structure,
		type,
		enumeration,
	};

	auto queryText = "(variable_decl name: (identifier) @var_decl)"
		"(compound_variable_decl name: (identifier) @var_decl)"
		"(named_parameter_decl name : (identifier) @var_decl)"
		"(using_statement name: (identifier) @var_decl)"
		"(function_definition name: (identifier) @func_decl)"
		"(constant_definition name: (identifier) @const_decl)"
		"(struct_definition name: (identifier) @struct_decl)"
		"(type_definition name: (identifier) @type_definition)"
		"(enum_member name: (identifier) @enum_member)"
		;

	auto var_decl_query = ts_query_new(
		s_jaiLang,
		queryText,
		strlen(queryText),
		&error_offset,
		&error_type
	);

	TSQueryCursor* queryCursor = ts_query_cursor_new();
	ts_query_cursor_exec(queryCursor, var_decl_query, root_node);

	TSQueryMatch match;
	uint32_t index;
	int cursor = 0;

	auto queryTimer = Timer("query");

	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		auto& node = match.captures[index].node;
		auto captureIndex = match.captures[index].index;
		/*
		auto start = ts_node_start_point(node);
		auto end = ts_node_end_point(node);

		SemanticToken token;
		token.col = start.column;
		token.line = start.row;
		token.length = end.column - start.column;
		token.modifier = (TokenModifier)0;
		*/

		auto type = (DeclType)captureIndex;

		switch (type)
		{
		case DeclType::variable:
			declMap[GetIdentifier(node, code)] = TokenType::Variable;
			break;
		case DeclType::function:
			declMap[GetIdentifier(node, code)] = TokenType::Function;
			break;
		case DeclType::constant:
			declMap[GetIdentifier(node, code)] = TokenType::EnumMember; // enum member for green.
			break;
		case DeclType::structure:
			declMap[GetIdentifier(node, code)] = TokenType::Type; // type gets us the nice teal.
			break;
		case DeclType::type:
			declMap[GetIdentifier(node, code)] = TokenType::Type;
			break;
		case DeclType::enumeration:
			declMap[GetIdentifier(node, code)] = TokenType::EnumMember;
			break;
		default:
			break;
		}
	}

	queryTimer.LogTimer();


	auto tokenTimer = Timer("Token add");
	AddVaraibleReferenceTokens(code, 0, 0, root_node, s_tokens, declMap, buffer);
	tokenTimer.LogTimer();

	ts_query_cursor_delete(queryCursor);
	ts_query_delete(var_decl_query);
	ts_tree_delete(tree);
	free(code);

	*outTokens = s_tokens.data();
	*count = s_tokens.size();
	totalTimer.LogTimer();
	return totalTimer.GetMicroseconds();
}
