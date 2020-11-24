#pragma once
#include <unordered_map>
#include <optional>
#include <shared_mutex>
#include <tree_sitter/api.h>
#include "Concurrent.h"
#include "GapBuffer.h"
#include "Scope.h"
#include <cassert>

#define export_jai_lsp extern "C" __declspec(dllexport)
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




struct Range
{
	int startRow, startCol;
	int endRow, endCol;
};









struct FileScope;

struct Module
{
	FileScope* moduleFile;
	Hash moduleFileHash;

	Scope exportedScope;
	void BuildExportedScope();
	std::optional<ScopeDeclaration> Search(Hash hash);
	int SearchAndGetFile(Hash hash, FileScope** outFile, Scope** declScope);
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
	TSSymbol enumDecl;
	TSSymbol unionDecl;
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
	TSSymbol functionCall;
	TSSymbol argument;
	TSSymbol usingStatement;
	TSSymbol usingExpression;
};

extern Constants g_constants;
extern ConcurrentDictionary<TSTree*> g_trees;
extern ConcurrentDictionary<GapBuffer*> g_buffers;
extern ConcurrentDictionary<std::string> g_filePaths;
extern ConcurrentVector<std::string> g_modulePaths;
extern ConcurrentVector<FileScope*> g_fileScopeByIndex;

std::string DebugNode(const TSNode& node, const GapBuffer* gb);
std::string_view GetIdentifier(const TSNode& node, std::string_view code);
Hash GetIdentifierHash(const TSNode& node, std::string_view code);
Hash GetIdentifierHash(const TSNode& node, const GapBuffer* buffer);
bool GetScopeForNode(const TSNode& node, FileScope* scope, ScopeHandle* handle);
bool GetScopeAndParentForNode(const TSNode& node, FileScope* scope, TSNode* outParentNode, ScopeHandle* handle);
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

export_jai_lsp long long CreateTree(const char* documentPath, const char* code, int length);
