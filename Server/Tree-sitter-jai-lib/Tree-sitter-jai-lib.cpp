// Tree-sitter-jai-lib.cpp : Defines the functions for the static library.
//

#include "framework.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <tree_sitter/api.h>

#include <vector>
#include <unordered_map>
#include <string_view>
#include <stack>
//#include <parser.h>

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
	std::string_view name;
	TokenType type;
};


struct Scope
{
	std::vector<ScopeEntry> entries;
	std::string_view content;
	TSNode node;
};


typedef std::unordered_map<const void*, Scope> ScopeMap;

extern "C" TSLanguage* tree_sitter_jai();
static TSLanguage* s_jaiLang;

static char names[1000];

static TSSymbol s_constDecl;
static TSSymbol s_varDecl;
static TSSymbol s_funcDecl;
static TSSymbol s_structDecl;

export int Init()
{
	s_jaiLang = tree_sitter_jai();
	s_constDecl = ts_language_symbol_for_name(s_jaiLang, "constant_value_definition", strlen("constant_value_definition"), true);
	s_varDecl = ts_language_symbol_for_name(s_jaiLang, "variable_decl", strlen("variable_decl"), true);
	s_funcDecl = ts_language_symbol_for_name(s_jaiLang, "function_definition", strlen("function_definition"), true);
	s_structDecl = ts_language_symbol_for_name(s_jaiLang, "struct_decl", strlen("struct_decl"), true);
	//nameId = ts_language_field_id_for_name(jaiLang, "name", strlen("name"));
	return 69420;
}

std::string_view GetIdentifier(const TSNode& node, const char* code)
{
	auto start = ts_node_start_byte(node);
	auto end = ts_node_end_byte(node);
	auto length = end - start;
	return std::string_view(&code[start], length);
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
		"(struct_decl) @local.scope"
		"(for_loop) @local.scope"

		"(variable_decl name : (identifier) @local.definition)"
		"(variable_decl names : (identifier) @local.definition)"
		"(implicit_variable_decl name : (identifier) @local.definition)"
		"(parameter_decl name : (identifier) @local.definition)"
		"(for_loop name : (identifier) @local.definition)"
		"(for_loop names : (identifier) @local.definition)"
		"(constant_value_definition name : (identifier) @local.definition)"
		"(struct_decl name : (identifier) @export.definition)"
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
		local,
		file,
		definition,
		exportDefn,
	};

	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		ScopeMarker captureType = (ScopeMarker)match.captures[index].index;
		auto& node = match.captures[index].node;
		switch (captureType)
		{
		case ScopeMarker::local:
		{	
			Scope localScope;
			auto start = ts_node_start_byte(node);
			auto length = ts_node_end_byte(node) - start;
			localScope.content = std::string_view(&code[start], length);
			localScope.node = node;
			scopeMap[node.id] = localScope;
			break;
		}
		case ScopeMarker::file:
			break;
		case ScopeMarker::definition:
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



export const char* GetCompletionItems(const char* code, int row, int col)
{
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
			if (entry.position.row < row || (entry.position.row == row && entry.position.column < col) )
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
}



export void GetTokens(const char* code, SemanticToken** tokens, int* count)
{
	static std::vector<SemanticToken> s_tokens;
	static std::unordered_map<std::string_view, TokenType> s_declMap;

	s_tokens.clear();
	s_declMap.clear();

	TSParser* parser = ts_parser_new();

	// Set the parser's language 
	ts_parser_set_language(parser, tree_sitter_jai());

	// Build a syntax tree based on source code stored in a string.
	auto tree = ts_parser_parse_string(
		parser,
		NULL,
		code,
		strlen(code));

	TSNode root_node = ts_tree_root_node(tree);
	uint32_t error_offset;
	TSQueryError error_type;
	auto queryText = "(variable_decl name: (identifier) @var_decl)"
		"(implicit_variable_decl name: (identifier) @var_decl)"
		"(function_definition name: (identifier) @func_decl)"
		"(constant_value_definition name: (identifier) @const_decl)"
		"(variable_reference name: (identifier) @var_ref)"
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

		if (captureIndex == 0) // var_decl 
		{
			token.type = TokenType::Variable;
			s_declMap[GetIdentifier(node, code)] = TokenType::Variable;
		}
		else if (captureIndex == 1) // func_decl
		{
			token.type = TokenType::Function;
			s_declMap[GetIdentifier(node, code)] = TokenType::Function;
		}
	//	else if (captureIndex == 2) // func  call
	//	{
	//		token.type = TokenType::Function;
	//	}
		else if (captureIndex == 2) // const_decl 
		{
			token.type = TokenType::EnumMember;
			s_declMap[GetIdentifier(node, code)] = TokenType::EnumMember;
		}
		else if (captureIndex == 3) // var_ref 
		{
			auto search = s_declMap.find(GetIdentifier(node, code));
			if (search != s_declMap.end())
			{
				token.type = search->second;
			}
			else
			{
				continue;
			}
		}
		
		s_tokens.push_back(token);
	}

	ts_tree_delete(tree);
	ts_parser_delete(parser);

	*tokens = s_tokens.data();
	*count = s_tokens.size();
}
