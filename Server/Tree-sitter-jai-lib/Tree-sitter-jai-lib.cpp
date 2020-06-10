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




ConcurrentDictionary<TSTree*> g_trees;
ConcurrentDictionary<GapBuffer*> g_buffers;
ConcurrentDictionary<ModuleScope*> g_modules;
ConcurrentDictionary<FileScope*> g_fileScopes;

TSLanguage* g_jaiLang;
Constants g_constants;

static char names[1000];
export long long UpdateTree(Hash documentHash);

static std::unordered_map<Hash, Type*> s_builtInTypes;

static void SetupBuiltInTypes()
{
	s_builtInTypes[StringHash("bool")] = new Type{ .name = "bool" };
	s_builtInTypes[StringHash("float32")] = new  Type{ .name = "float32" };
	s_builtInTypes[StringHash("float")] = s_builtInTypes[StringHash("float32")];
	s_builtInTypes[StringHash("float64")] = new Type{ .name = "float64" };
	s_builtInTypes[StringHash("char")] = new Type{ .name = "char" };
	s_builtInTypes[StringHash("string")] = new Type{ .name = "string" }; // string has some members like data and length.
	s_builtInTypes[StringHash("s8")] = new Type{ .name = "s8" };
	s_builtInTypes[StringHash("s16")] = new Type{ .name = "s16" };
	s_builtInTypes[StringHash("s32")] = new Type{ .name = "s32" };
	s_builtInTypes[StringHash("s64")] = new Type{ .name = "s64" };
	s_builtInTypes[StringHash("int")] = s_builtInTypes[StringHash("s64")]; // s64?
	s_builtInTypes[StringHash("u8")] = new Type{ .name = "u8" };
	s_builtInTypes[StringHash("u16")] = new Type{ .name = "u16" };
	s_builtInTypes[StringHash("u32")] = new Type{ .name = "u32" };
	s_builtInTypes[StringHash("u64")] = new Type{ .name = "u64" };
	s_builtInTypes[StringHash("void")] = new Type{ .name = "void" };

}

export int Init()
{
	g_jaiLang = tree_sitter_jai();

	g_constants.constDecl = ts_language_symbol_for_name(g_jaiLang, "constant_definition", strlen("constant_definition"), true);
	g_constants.varDecl = ts_language_symbol_for_name(g_jaiLang, "named_decl", strlen("named_decl"), true);
	g_constants.funcDecl = ts_language_symbol_for_name(g_jaiLang, "named_block_decl", strlen("named_block_decl"), true);
	g_constants.structDecl = ts_language_symbol_for_name(g_jaiLang, "struct_definition", strlen("struct_definition"), true);
	g_constants.import = ts_language_symbol_for_name(g_jaiLang, "import_statement", strlen("import_statement"), true);
	g_constants.structDecl = ts_language_symbol_for_name(g_jaiLang, "struct_definition", strlen("struct_definition"), true);
	g_constants.memberAccess = ts_language_symbol_for_name(g_jaiLang, "member_access", strlen("member_access"), true);

	SetupBuiltInTypes();
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

	if (symbol == g_constants.constDecl)
		return TokenType::EnumMember;
		
	if (symbol == g_constants.varDecl)
		return TokenType::Variable;

	if (symbol == g_constants.funcDecl)
		return TokenType::Function;

	if (symbol == g_constants.structDecl)
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
		auto symbolName = ts_language_symbol_name(g_jaiLang, parentSymbol);

		parent = ts_node_parent(parent);
	}

	return &scope->scopes[parent.id];
}

static std::vector<std::string_view> BuildModuleScope(Hash document, const char* moduleName)
{
	auto tree = ts_tree_copy(g_trees.Read(document).value());
	auto root = ts_tree_root_node(tree);
	auto buffer = g_buffers.Read(document).value();
	auto moduleHash = StringHash(moduleName);

	auto scopeOpt = g_modules.Read(moduleHash);
	ModuleScope* scope;

	if (!scopeOpt)
	{
		scope = new ModuleScope();
		g_modules.Write(moduleHash, scope);
	}
	else
	{
		scope = scopeOpt.value();
	}

	
	std::vector<std::string_view> loads;

	auto queryText =
		"(source_file (named_decl (names (identifier) @definition)))"
		"(source_file (named_decl (names (identifier) @func_defn) (function_definition (function_header) @func_defn)))"
		"(source_file (named_decl (names (identifier) @struct_defn) (struct_definition)))"
		
		"(export_scope_directive) @export"
		"(file_scope_directive) @file"
		"(load_directive name: (string_literal) @load)"
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
			ModuleScopeDeclaration entry;
			entry.definitionNode = node;
			entry.tokenType = GetTokenTypeForNode(node);
			entry.definingFile = document;
#if _DEBUG
			entry.name = GetIdentifierFromBufferCopy(node, buffer);
#endif
			scope->entries[GetIdentifierHash(node, code)] = entry;
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

			ModuleScopeDeclaration entry;
			entry.definitionNode = node;
			entry.tokenType = TokenType::Function;
			entry.definingFile = document;

#if _DEBUG
			entry.name = GetIdentifierFromBufferCopy(node, buffer);
#endif

			TSQueryMatch typeMatch;
			uint32_t nextTypeIndex;
			ts_query_cursor_next_capture(queryCursor, &typeMatch, &nextTypeIndex);
			auto headerNode = typeMatch.captures[nextTypeIndex].node;

			Type* type = new Type();
			type->name = GetIdentifierFromBuffer(headerNode, buffer).CopyMalloc();
			entry.type = type;

			scope->entries[GetIdentifierHash(node, code)] = entry;
			break;
		}
		case Capture::struct_defn:
		{
			if (!exporting)
				continue;

			Type* type = new Type(); // leak this 
			type->name = GetIdentifierFromBuffer(node, buffer).CopyMalloc(); // leak this too!

			ModuleScopeDeclaration entry;
			entry.definitionNode = node;
			entry.tokenType = TokenType::Type;
			entry.definingFile = document;
			entry.type = type;

#if _DEBUG
			entry.name = GetIdentifierFromBufferCopy(node, buffer);
#endif
			scope->entries[GetIdentifierHash(node, code)] = entry;
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

	// ts_tree_delete(tree);
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

static ScopeDeclaration AddEntryToScope(TSNode node, TokenType tokenType, GapBuffer* buffer, Scope* scope, Type* type)
{
	ScopeDeclaration entry;
	entry.definitionNode = node;
	entry.tokenType = tokenType;
	entry.type = type;
#if _DEBUG
	entry.name = GetIdentifierFromBufferCopy(node, buffer);
#endif

	scope->entries[GetIdentifierHash(node, buffer)] = entry;
	return entry;
}

static ScopeDeclaration AddEntry(TSNode node, TokenType tokenType, FileScope* fileScope, GapBuffer* buffer, Type* type)
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
	token.type = tokenType;
	fileScope->tokens.push_back(token);

	return AddEntryToScope(node, tokenType, buffer, scope, type);
}






Type* EvaluateNodeExpressionType(TSNode node, GapBuffer* buffer, const std::vector<Scope*>& scopeKing, FileScope* fileScope)
{
	static auto builtInTypeSymbol = ts_language_symbol_for_name(g_jaiLang, "built_in_type", strlen("built_in_type"), true);
	static auto identifierSymbol = ts_language_symbol_for_name(g_jaiLang, "identifier", strlen("identifier"), true);

	auto symbol = ts_node_symbol(node);
	auto hash = GetIdentifierHash(node, buffer);

	if (symbol == builtInTypeSymbol)
	{
		return s_builtInTypes[hash];
	}
	else if (symbol == identifierSymbol)
	{
		// identifier of some kind. struct, union, or enum.
		for (int sp = 0; sp < scopeKing.size(); sp++)
		{
			int index = scopeKing.size() - sp - 1;
			auto frame = scopeKing[index];
			if (frame->entries.contains(hash))
			{
				return frame->entries[hash].type;
			}
		}

		// search modules
		for (auto moduleHash : fileScope->imports)
		{
			auto moduleScope = g_modules.Read(moduleHash);
			if (moduleScope && moduleScope.value()->entries.contains(hash))
			{
				return moduleScope.value()->entries[hash].type;
			}
		}
	}

	// unresolved type or expression of some kind.
	return nullptr;
}


static void BuildFileScope(Hash documentHash)
{
	FileScope* fileScope = new FileScope();
	g_fileScopes.Write(documentHash, fileScope);

	fileScope->imports.clear();
	fileScope->scopes.clear();
	fileScope->tokens.clear();
	fileScope->file.entries.clear();

	auto buffer = g_buffers.Read(documentHash).value();

	auto tree = ts_tree_copy(g_trees.Read(documentHash).value());
	auto root = ts_tree_root_node(tree);
	

	auto queryText =
		"(source_file) @file.scope"
		"(data_scope) @data.scope"
		"(imperative_scope) @imperative.scope"

		"(named_decl (names (identifier) @const_decl) (const_initializer))"
		"(named_decl (names (identifier) @var_decl) (variable_initializer))"
		"(named_decl (names (identifier) @var_decl) type: (*) @expr )"
		"(named_decl (names (identifier) @func_defn) (function_definition (function_header) @func_defn))"
		"(named_decl (names (identifier) @struct_defn) (struct_definition))"
		"(named_decl (names (identifier) @enum_defn) (enum_definition))"
		"(parameter (identifier) @parameter_decl)"
		"(identifier) @var_ref"
		"(block_end) @block_end"
		"(import_statement name: (string_literal) @import)"
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
	ts_query_cursor_exec(queryCursor, query, root);

	TSQueryMatch match;
	uint32_t index;

	enum class ScopeMarker
	{
		fileScope,
		dataScope,
		imperativeScope,
		const_decl,
		var_decl,
		expr,
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
	Type* currentType = nullptr;

	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		ScopeMarker captureType = (ScopeMarker)match.captures[index].index;
		auto node = match.captures[index].node;
		switch (captureType)
		{
		case ScopeMarker::dataScope:
		{
			auto localScope = &fileScope->scopes[node.id];
			localScope->imperative = false;
			auto start = ts_node_start_byte(node);
			auto end = ts_node_end_byte(node);
#if _DEBUG
			localScope->content = buffer_view(start, end, buffer);
#endif
			// if we have any paremeters, inject them here.
			for (int i = 0; i < parameters.size(); i++)
			{
				AddEntryToScope(parameters[i], TokenType::Variable, buffer, localScope, nullptr);
			}
			parameters.clear();
			
			if(currentType)
				currentType->members = localScope;

			currentType = nullptr;

			scopeKing.push_back(localScope);
			break;
		}
		case ScopeMarker::imperativeScope:
		{
			auto localScope = &fileScope->scopes[node.id];
			localScope->imperative = true;
			auto start = ts_node_start_byte(node);
			auto end = ts_node_end_byte(node);
#if _DEBUG
			localScope->content = buffer_view(start, end, buffer);
#endif
			// if we have any paremeters, inject them here.
			for (int i = 0; i < parameters.size(); i++)
			{
				AddEntryToScope(parameters[i], TokenType::Variable, buffer, localScope, nullptr);
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
			// uhhh just do all the imports now lol!

			auto named_count = ts_node_named_child_count(node);
			auto cursor = ts_tree_cursor_new(node);
			if (ts_tree_cursor_goto_first_child(&cursor))
			{
				//non empty tree:
				while (ts_tree_cursor_goto_next_sibling(&cursor))
				{
					auto current = ts_tree_cursor_current_node(&cursor);
					if (ts_node_symbol(current) == g_constants.import)
					{
						auto nameNode = ts_node_named_child(current, 0);
						auto startOffset = ts_node_start_byte(nameNode) + 1;
						auto endOffset = ts_node_end_byte(nameNode) - 1;
						buffer_view view = buffer_view(startOffset, endOffset, buffer);
						auto moduleNameHash = StringHash(view);
						fileScope->imports.push_back(moduleNameHash);
					}
				}
			}

			ts_tree_cursor_delete(&cursor);
			scopeKing.push_back(bigScope);
			break;
		}
		case ScopeMarker::var_decl:
		{
			// get the expr node
			TSQueryMatch typeMatch;
			uint32_t nextTypeIndex;
			ts_query_cursor_next_capture(queryCursor, &typeMatch, &nextTypeIndex);
			ts_query_cursor_next_capture(queryCursor, &typeMatch, &nextTypeIndex);

			ScopeMarker captureType = (ScopeMarker)typeMatch.captures[nextTypeIndex].index;
			auto exprNode = typeMatch.captures[nextTypeIndex].node;

			Type* exprType = nullptr;
			if (captureType == ScopeMarker::expr)
			{
				// evaluate the type of this node lmfao
				exprType = EvaluateNodeExpressionType(exprNode, buffer, scopeKing, fileScope);
			}

			AddEntry(node, TokenType::Variable, fileScope, buffer, exprType);
			break;
		}
		
		case ScopeMarker::const_decl:
		{
			skipNextIdentifier = true;
			AddEntry(node, TokenType::Number, fileScope, buffer, nullptr);
			break;
		}
		
		case ScopeMarker::function_defn:
		{
			TSQueryMatch typeMatch;
			uint32_t nextTypeIndex;
			ts_query_cursor_next_capture(queryCursor, &typeMatch, &nextTypeIndex); // skip identifier
			ts_query_cursor_next_capture(queryCursor, &typeMatch, &nextTypeIndex);

			ScopeMarker captureType = (ScopeMarker)typeMatch.captures[nextTypeIndex].index;
			auto headerNode = typeMatch.captures[nextTypeIndex].node;

			Type* type = new Type();
			type->name = GetIdentifierFromBuffer(headerNode, buffer).CopyMalloc();

			AddEntry(node, TokenType::Function, fileScope, buffer, type);
			break;
		}
		
		case ScopeMarker::struct_defn:
		{
			skipNextIdentifier = true;
			Type* type = new Type(); // leak this 
			type->name = GetIdentifierFromBuffer(node, buffer).CopyMalloc(); // leak this too!
			currentType = type;
			AddEntry(node, TokenType::Type, fileScope, buffer, type);
			break;
		}
		case ScopeMarker::enum_defn:
		{
			skipNextIdentifier = true;
			AddEntry(node, TokenType::Enum, fileScope, buffer, nullptr);
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
						token.type = frame->entries[hash].tokenType;
						goto done;
					}
				}

				// search modules
				for (auto moduleHash : fileScope->imports)
				{
					auto moduleScope = g_modules.Read(moduleHash);
					if(moduleScope && moduleScope.value()->entries.contains(hash) )
					{
						token.type = moduleScope.value()->entries[hash].tokenType;
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
			break;
		}
		case ScopeMarker::import:
		{
			break;
			/*
			// elide the quotation marks
			auto startOffset = ts_node_start_byte(node) + 1;
			auto endOffset = ts_node_end_byte(node) - 1;
			buffer_view view = buffer_view(startOffset, endOffset, buffer);
			auto moduleNameHash = StringHash(view);
			fileScope->imports.push_back(moduleNameHash);
			*/
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
						fileScope->tokens[index].type = entry.tokenType;
						foundInLocalScopes = true;
						break;
					}
				}
				else
				{
					auto entry = scope->entries[identifierHash];
					fileScope->tokens[index].type = entry.tokenType;
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
			fileScope->tokens[index].type = entry.tokenType;
		}
		else
		{
			// search modules
			bool found = false;
			for (auto moduleHash : fileScope->imports)
			{
				auto moduleScope = g_modules.Read(moduleHash);
				if (moduleScope && moduleScope.value()->entries.contains(identifierHash))
				{
					auto entry = moduleScope.value()->entries[identifierHash];
					fileScope->tokens[index].type = entry.tokenType;
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

export long long EditTree(Hash documentHash, const char* change, int startLine, int startCol, int endLine, int endCol, int contentLength, int rangeLength)
{
	auto timer = Timer("");

	auto buffer = g_buffers.Read(documentHash).value();
	auto edit = buffer->Edit(startLine, startCol, endLine, endCol, change, contentLength, rangeLength);
	auto tree = g_trees.Read(documentHash).value();
	ts_tree_edit(tree, &edit);
	g_trees.Write(documentHash, tree);

	return timer.GetMicroseconds();
}


export long long CreateTree(Hash documentHash, const char* code, int length)
{
	auto timer = Timer("");
	
	auto bufferOpt = g_buffers.Read(documentHash);
	GapBuffer* buffer;

	if (bufferOpt)
	{
		buffer = bufferOpt.value();
		*buffer = GapBuffer(code, length);
	}
	else
	{
		buffer = new GapBuffer(code, length);
		g_buffers.Write(documentHash, buffer);
	}

	auto treeOpt = g_trees.Read(documentHash);

	if (treeOpt)
	{
		ts_tree_delete(treeOpt.value());;
	}

	TSParser* parser = ts_parser_new();
	ts_parser_set_language(parser, g_jaiLang);

	auto view = buffer->GetEntireStringView();
	auto tree = ts_parser_parse_string(parser, nullptr, view.data(), length);

	g_trees.Write(documentHash, tree);

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

	BuildFileScope(documentHash);

	return timer.GetMicroseconds();
}



export long long GetTokens(Hash documentHash, SemanticToken** outTokens, int* count)
{
	auto t = Timer("");
	auto fileScope = g_fileScopes.Read(documentHash).value();
	*outTokens = fileScope->tokens.data();
	*count = fileScope->tokens.size();

	return t.GetMicroseconds();
}
