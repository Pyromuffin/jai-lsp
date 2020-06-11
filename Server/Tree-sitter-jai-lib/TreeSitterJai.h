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
	 TSSymbol load;
	 TSSymbol builtInType;
	 TSSymbol identifier;
};

extern Constants g_constants;

enum class TokenType : uint8_t
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

enum class TokenModifier : uint8_t
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

struct Scope;

struct Type
{
	const char* name;
	Scope* members;
	Hash documentHash;
};


enum DeclarationFlags : uint8_t
{
	Exported = 1 << 0,
	Constant = 1 << 1,
	Struct = 1 << 2,
	Enum = 1 << 3,
	Function =  1 << 4,
};

struct ScopeDeclaration
{
	//TokenType tokenType; // can be derived from type*
	//TSNode definitionNode; // can probably be figured out from search.
	DeclarationFlags flags;
	uint16_t length;
	uint32_t startByte;
	Type* type; // maybe make this a handle.
};

inline DeclarationFlags operator|(DeclarationFlags a, DeclarationFlags b)
{
	return static_cast<DeclarationFlags>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr auto size = sizeof(ScopeDeclaration);

struct Scope
{
	bool imperative;
	std::unordered_map<Hash, ScopeDeclaration> declarations;

#if _DEBUG
	buffer_view content;
#endif
	
	std::optional<ScopeDeclaration> TryGet(Hash hash)
	{
		auto it = declarations.find(hash);
		if (it == declarations.end())
			return std::nullopt;

		return std::optional<ScopeDeclaration>(it->second);
	}

	void AppendMembers(std::string& str, GapBuffer* buffer, uint32_t upTo = UINT_MAX)
	{
		for (auto& kvp : declarations)
		{
			if (imperative && (int)kvp.second.startByte > upTo)
				continue;

			for (int i = 0; i < kvp.second.length; i++)
				str.push_back(buffer->GetChar(kvp.second.startByte + i));

			str.push_back(',');
		}
	}

	void AppendExportedMembers(std::string& str, GapBuffer* buffer)
	{
		for (auto& kvp : declarations)
		{
			if (kvp.second.flags & DeclarationFlags::Exported)
			{
				for (int i = 0; i < kvp.second.length; i++)
					str.push_back(buffer->GetChar(kvp.second.startByte + i));

				str.push_back(',');
			}
		}
	}

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




struct FileScope;

struct Module
{
	bool dirty;
	FileScope* moduleFile;
	Hash moduleFileHash;

	Scope exportedScope;
	void BuildExportedScope();
	std::optional<ScopeDeclaration> Search(Hash hash);
};


extern ConcurrentDictionary<Module*> g_modules;
extern ConcurrentDictionary<FileScope*> g_fileScopes;


struct FileScope
{
	std::vector<Hash> imports;
	std::vector<Hash> loads;
	std::unordered_map<const void*, Scope> scopes;
	std::vector<SemanticToken> tokens;
	Scope file;


	//  oooooo kkkkk ay
	// i realize now that imports CAN be exported and we probably need to account for this.
	// this is evidenced by the fact that sometimes imports are declared in file scope, presumably so they don't leak out to the module.

	std::optional<ScopeDeclaration> SearchExports(Hash identifierHash)
	{
		if (auto decl = file.TryGet(identifierHash))
		{
			if(decl.value().flags & DeclarationFlags::Exported)
				return decl;
		}

		for (auto loadHash : loads)
		{
			if (auto file = g_fileScopes.Read(loadHash))
			{
				if (auto decl = file.value()->SearchExports(identifierHash))
				{
					return decl;
				}
			}
		}

		return std::nullopt;
	}

	std::optional<ScopeDeclaration> SearchModules(Hash identifierHash)
	{
		for (auto modHash : imports)
		{
			if (auto mod = g_modules.Read(modHash))
			{
				if (auto decl = mod.value()->Search(identifierHash))
				{
					return decl;
				}
			}
		}

		for (auto loadHash : loads)
		{
			if (auto file = g_fileScopes.Read(loadHash))
			{
				if (auto decl = file.value()->SearchExports(identifierHash))
				{
					return decl;
				}
			}
		}

		return std::nullopt;
	}


	std::optional<ScopeDeclaration> Search(Hash identifierHash)
	{
		if (auto decl = file.TryGet(identifierHash))
		{
			return decl;
		}

		return SearchModules(identifierHash);
	}
};




extern ConcurrentDictionary<TSTree*> g_trees;
extern ConcurrentDictionary<GapBuffer*> g_buffers;
extern ConcurrentDictionary<std::string> g_filePaths;

std::string_view GetIdentifier(const TSNode& node, std::string_view code);
Hash GetIdentifierHash(const TSNode& node, std::string_view code);
Hash GetIdentifierHash(const TSNode& node, GapBuffer* buffer);
Scope* GetScopeForNode(const TSNode& node, FileScope* scope);
Scope* GetScopeAndParentForNode(const TSNode& node, FileScope* scope, TSNode* outParentNode);
Type* GetTypeForNode(TSNode node, FileScope* fileScope, GapBuffer* buffer);
export long long CreateTree(const char* documentPath, const char* code, int length);
