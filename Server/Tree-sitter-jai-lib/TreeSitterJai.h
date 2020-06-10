#pragma once
#include <unordered_map>
#include <optional>
#include <shared_mutex>
#include <tree_sitter/api.h>
#include "GapBuffer.h"

#define export extern "C" __declspec(dllexport)
extern "C" TSLanguage * tree_sitter_jai();
extern TSLanguage* g_jaiLang;


struct Constants
{
	 TSSymbol constDecl;
	 TSSymbol import;
	 TSSymbol varDecl;
	 TSSymbol funcDecl;
	 TSSymbol structDecl;
	 TSSymbol memberAccess;
};

extern Constants g_constants;

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

struct Type;
struct Scope;

struct Member
{
	const char* name;
	Type* type;
};

struct Type
{
	const char* name;
	Scope* members;
};

struct ScopeDeclaration
{
	TSNode definitionNode;
	TokenType tokenType;
	Type* type;
#if _DEBUG
	std::string name;
#endif
};

struct Scope
{
	bool imperative;
	std::unordered_map<Hash, ScopeDeclaration> entries;

#if _DEBUG
	buffer_view content;
#endif
	
	std::optional<ScopeDeclaration> TryGet(Hash hash)
	{
		auto it = entries.find(hash);
		if (it == entries.end())
			return std::nullopt;

		return std::optional<ScopeDeclaration>(it->second);
	}
};

struct ModuleScopeDeclaration
{
	TSNode definitionNode;
	Hash definingFile;
	TokenType tokenType; // might not need this anymore because we can get it from type
	Type* type;

#if _DEBUG
	std::string name;
#endif

};

struct ModuleScope
{
	std::unordered_map<Hash, ModuleScopeDeclaration> entries;

#if _DEBUG
	buffer_view content;
#endif

	std::optional<ModuleScopeDeclaration> TryGet(Hash hash)
	{
		auto it = entries.find(hash);
		if (it == entries.end())
			return std::nullopt;

		return std::optional<ModuleScopeDeclaration>(it->second);
	}


};

struct FileScope
{
	std::vector<Hash> imports;
	std::unordered_map<const void*, Scope> scopes;
	std::vector<SemanticToken> tokens;
	Scope file;
};


template <typename T>
struct ConcurrentDictionary
{
	std::shared_mutex mutex;
	std::unordered_map<Hash, T> dict;

	std::optional<T> Read(Hash key)
	{
		mutex.lock_shared();
		auto it = dict.find(key);

		if (it == dict.end())
		{
			mutex.unlock_shared();
			return std::nullopt;
		}

		auto value = it->second;
		mutex.unlock_shared();
		return std::optional(value);
	}

	void Write(Hash key, T value)
	{
		mutex.lock();
		dict[key] = value;
		mutex.unlock();
	}
};


extern ConcurrentDictionary<TSTree*> g_trees;
extern ConcurrentDictionary<GapBuffer*> g_buffers;
extern ConcurrentDictionary<ModuleScope*> g_modules;
extern ConcurrentDictionary<FileScope*> g_fileScopes;

std::string_view GetIdentifier(const TSNode& node, std::string_view code);
Hash GetIdentifierHash(const TSNode& node, std::string_view code);
Hash GetIdentifierHash(const TSNode& node, GapBuffer* buffer);
Scope* GetScopeForNode(const TSNode& node, FileScope* scope);
Scope* GetScopeAndParentForNode(const TSNode& node, FileScope* scope, TSNode* outParentNode);
Type* GetTypeForNode(TSNode node, FileScope* fileScope, GapBuffer* buffer);
