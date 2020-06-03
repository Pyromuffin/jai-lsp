#pragma once
#include <unordered_map>
#include <tree_sitter/api.h>
#include "GapBuffer.h"

#define export extern "C" __declspec(dllexport)
extern "C" TSLanguage * tree_sitter_jai();

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
	TSPoint definitionPosition;
	TokenType type;
#if _DEBUG
	std::string name;
#endif
};

struct Scope
{
	std::unordered_map<Hash, ScopeEntry> entries;
	//TSNode node; // this is used for finding the parent scope of a scope so that we can inject things into the outer scope

#if _DEBUG
	buffer_view content;
#endif
	
};

struct FileScope
{
	std::vector<Hash> imports;
	std::unordered_map<const void*, Scope> scopes;
	std::vector<SemanticToken> tokens;
	Scope file;
};

extern std::unordered_map<Hash, TSTree*> g_trees;
extern std::unordered_map<Hash, GapBuffer> g_buffers;
extern std::unordered_map<Hash, Scope> g_modules;
extern std::unordered_map<Hash, FileScope> g_fileScopes;

std::string_view GetIdentifier(const TSNode& node, std::string_view code);
buffer_view GetIdentifier(const TSNode& node, GapBuffer* buffer);
Hash GetIdentifierHash(const TSNode& node, std::string_view code);
Hash GetIdentifierHash(const TSNode& node, GapBuffer* buffer);
Scope* GetScopeForNode(const TSNode& node, FileScope* scope);
