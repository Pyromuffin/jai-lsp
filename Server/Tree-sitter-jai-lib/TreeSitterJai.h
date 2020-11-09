#pragma once
#include <unordered_map>
#include <optional>
#include <shared_mutex>
#include <tree_sitter/api.h>
#include "GapBuffer.h"

#define export extern "C" __declspec(dllexport)
extern "C" TSLanguage * tree_sitter_jai();
extern TSLanguage* g_jaiLang;




enum class TokenType : uint8_t
{
	Documentation,
	Comment,
	Keyword,
	String,
	Number, // light green, like a constant.
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
	EnumMember, // keyword blue
};

enum class TokenModifier : uint8_t
{
	Documentation = 1 << 0,
	Declaration = 1 << 1,
	Definition = 1 << 2,
	Static = 1 << 3,
	Abstract = 1 << 4,
	Deprecated = 1 << 5, // seems to effect variables to be dark grey
	Readonly = 1 << 6, // seems to effect variables to be darker blue
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
struct ScopeDeclaration;


struct TypeKing
{
	enum Kind
	{
		structure,
		function
	};

	std::string name;
	std::vector<std::string> parameters;
	Scope* members;
};


struct TypeHandle
{
	uint16_t fileIndex;
	uint16_t index;

	static constexpr TypeHandle Null()
	{
		return TypeHandle{ .fileIndex = UINT16_MAX, .index = UINT16_MAX };
	}

	bool operator==(const TypeHandle& rhs)
	{
		return fileIndex == rhs.fileIndex && index == rhs.index;
	}

};


enum DeclarationFlags : uint8_t
{
	None = 0,
	Exported = 1 << 0,
	Constant = 1 << 1,
	Struct = 1 << 2,
	Enum = 1 << 3,
	Function =  1 << 4,
	BuiltIn = 1 << 5,
};

struct ScopeDeclaration
{
	DeclarationFlags flags;
	uint16_t length;
	uint16_t fileIndex;
	uint32_t startByte;
	TypeHandle type;
};


inline DeclarationFlags operator|(DeclarationFlags a, DeclarationFlags b)
{
	return static_cast<DeclarationFlags>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr auto size = sizeof(ScopeDeclaration);
#define SMALL true


struct Scope
{
	static constexpr int small_size = 8;
	
private:
#if SMALL
	Hash small_hashes[small_size];
	ScopeDeclaration small_declarations[small_size];
#endif
	std::unordered_map<Hash, ScopeDeclaration> declarations;
	int size;

public:
	bool imperative;
	void Clear()
	{
		size = 0;
		declarations.clear();
	}

	std::optional<ScopeDeclaration> TryGet(const Hash hash)
	{
#if SMALL
		if (size < small_size)
		{
			for (int i = 0; i < size; i++)
			{
				if (small_hashes[i] == hash)
					return small_declarations[i];
			}

			return std::nullopt;
		}
#endif

		auto it = declarations.find(hash);
		if (it == declarations.end())
			return std::nullopt;

		return std::optional<ScopeDeclaration>(it->second);
	}

	void Add(const Hash hash, const ScopeDeclaration decl)
	{
#if SMALL
		if (size < small_size)
		{
			small_hashes[size] = hash;
			small_declarations[size] = decl;
			size++;

			return;
		}
		if (size == small_size)
		{
			for (int i = 0; i < small_size; i++)
			{
				auto small_hash = small_hashes[i];
				auto small_decl = small_declarations[i];
				declarations.insert({ small_hash, small_decl });
			}
		}
#endif

		declarations.insert({ hash, decl });
		size++;
	}


	void AppendMembers(std::string& str, const GapBuffer* buffer, uint32_t upTo = UINT_MAX)
	{
#if SMALL
		if (size < small_size)
		{
			for (int i = 0; i < size; i++)
			{
				auto decl = small_declarations[i];
				if (imperative && (int)decl.startByte > upTo)
					continue;

				for (int i = 0; i < decl.length; i++)
					str.push_back(buffer->GetChar(decl.startByte + i));
				
				str.push_back(',');
			}

			return;
		}
#endif

		for (auto& kvp : declarations)
		{
			if (imperative && (int)kvp.second.startByte > upTo)
				continue;

			for (int i = 0; i < kvp.second.length; i++)
				str.push_back(buffer->GetChar(kvp.second.startByte + i));

			str.push_back(',');
		}
	}

	void AppendExportedMembers(std::string& str, const GapBuffer* buffer)
	{
#if SMALL
		if (size < small_size)
		{
			for (int i = 0; i < size; i++)
			{
				auto decl = small_declarations[i];
				if (decl.flags & DeclarationFlags::Exported)
				{
					for (int i = 0; i < decl.length; i++)
						str.push_back(buffer->GetChar(decl.startByte + i));

					str.push_back(',');
				}
			}

			return;
		}
#endif
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


	void UpdateType(const Hash hash, const TypeHandle type)
	{
#if SMALL
		if (size < small_size)
		{
			for (int i = 0; i < size; i++)
			{
				if (small_hashes[i] == hash)
				{
					small_declarations[i].type = type;
					return;
				}
			}
		}
#endif

		declarations[hash].type = type;
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



struct Cursor
{
	TSTreeCursor cursor;

	Cursor()
	{
		cursor = ts_tree_cursor_new(TSNode());
	}

	~Cursor()
	{
		ts_tree_cursor_delete(&cursor);
	}

	void Reset(TSNode node)
	{
		ts_tree_cursor_reset(&cursor, node);
	}

	bool Child()
	{
		return ts_tree_cursor_goto_first_child(&cursor);
	}

	TSNode Current()
	{
		return ts_tree_cursor_current_node(&cursor);
	}

	bool Sibling()
	{
		return ts_tree_cursor_goto_next_sibling(&cursor);
	}

	bool Parent()
	{
		return ts_tree_cursor_goto_parent(&cursor);
	}

	TSSymbol Symbol()
	{
		return ts_node_symbol(Current());
	}

};


struct Constants
{
	TSSymbol constDecl;
	TSSymbol import;
	TSSymbol varDecl;
	TSSymbol funcDefinition;
	TSSymbol structDecl;
	TSSymbol memberAccess;
	TSSymbol memberAccessNothing;
	TSSymbol load;
	TSSymbol builtInType;
	TSSymbol identifier;
	TSSymbol namedDecl;
	TSSymbol scopeFile;
	TSSymbol scopeExport;
	TSSymbol dataScope;
	TSSymbol imperativeScope;
	TSSymbol parameter;

	std::unordered_map<Hash, TypeHandle> builtInTypes;
	TypeKing builtInTypesByIndex[14];
};

extern Constants g_constants;
extern ConcurrentDictionary<TSTree*> g_trees;
extern ConcurrentDictionary<GapBuffer*> g_buffers;
extern ConcurrentDictionary<std::string> g_filePaths;
extern std::vector<const FileScope*> g_fileScopeByIndex;


std::string_view GetIdentifier(const TSNode& node, std::string_view code);
Hash GetIdentifierHash(const TSNode& node, std::string_view code);
Hash GetIdentifierHash(const TSNode& node, const GapBuffer* buffer);
Scope* GetScopeForNode(const TSNode& node, FileScope* scope);
Scope* GetScopeAndParentForNode(const TSNode& node, FileScope* scope, TSNode* outParentNode);
std::optional<ScopeDeclaration> GetDeclarationForNode(TSNode node, FileScope* fileScope, const GapBuffer* buffer);
const TypeKing* GetTypeForNode(TSNode node, FileScope* fileScope, GapBuffer* buffer);
const TypeKing* GetType(TypeHandle handle);
TokenType GetTokenTypeFromFlags(DeclarationFlags flags);


struct Timings
{
	long long bufferTime;
	long long parseTime;
	long long scopeTime;
};

export long long CreateTree(const char* documentPath, const char* code, int length);
