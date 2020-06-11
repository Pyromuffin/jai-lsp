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
ConcurrentDictionary<Module*> g_modules;
ConcurrentDictionary<FileScope*> g_fileScopes;
ConcurrentDictionary<std::string> g_filePaths;

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
	g_constants.load = ts_language_symbol_for_name(g_jaiLang, "load_statement", strlen("load_statement"), true);
	g_constants.builtInType = ts_language_symbol_for_name(g_jaiLang, "built_in_type", strlen("built_in_type"), true);
	g_constants.identifier = ts_language_symbol_for_name(g_jaiLang, "identifier", strlen("identifier"), true);

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

static ScopeDeclaration AddEntryToScope(TSNode node, TokenType tokenType, GapBuffer* buffer, Scope* scope, Type* type, DeclarationFlags flags)
{
	ScopeDeclaration entry;
	entry.type = type;
	entry.flags = flags;
	auto start = ts_node_start_byte(node);
	auto end = ts_node_end_byte(node);

	entry.startByte = start;
	entry.length = end - start;

	scope->declarations[GetIdentifierHash(node, buffer)] = entry;
	return entry;
}

static ScopeDeclaration AddEntry(TSNode node, TokenType tokenType, FileScope* fileScope, GapBuffer* buffer, Type* type, DeclarationFlags flags)
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

	return AddEntryToScope(node, tokenType, buffer, scope, type, flags);
}


Type* EvaluateNodeExpressionType(TSNode node, GapBuffer* buffer, const std::vector<Scope*>& scopeKing, FileScope* fileScope)
{
	auto symbol = ts_node_symbol(node);
	auto hash = GetIdentifierHash(node, buffer);

	if (symbol == g_constants.builtInType)
	{
		return s_builtInTypes[hash];
	}
	else if (symbol == g_constants.identifier)
	{
		// identifier of some kind. struct, union, or enum.
		for (int sp = 0; sp < scopeKing.size(); sp++)
		{
			int index = scopeKing.size() - sp - 1;
			auto frame = scopeKing[index];
			if (frame->declarations.contains(hash))
			{
				return frame->declarations[hash].type;
			}
		}

		if (auto decl = fileScope->SearchModules(hash))
		{
			return decl.value().type;
		}
	}

	// unresolved type or expression of some kind.
	return nullptr;
}


void CreateScope(FileScope* fileScope, TSNode& node, GapBuffer* buffer, std::vector<TSNode>& parameters, Type*& currentType, std::vector<Scope*>& scopeKing, bool imperative, Hash document)
{
	auto localScope = &fileScope->scopes[node.id];
	localScope->imperative = imperative;
	auto start = ts_node_start_byte(node);
	auto end = ts_node_end_byte(node);
#if _DEBUG
	localScope->content = buffer_view(start, end, buffer);
#endif
	// if we have any paremeters, inject them here.
	for (int i = 0; i < parameters.size(); i++)
	{
		AddEntryToScope(parameters[i], TokenType::Variable, buffer, localScope, nullptr, (DeclarationFlags)0);
	}
	parameters.clear();

	if (currentType)
		currentType->members = localScope;

	currentType = nullptr;

	scopeKing.push_back(localScope);
}



void CreateTopLevelScope(FileScope* fileScope, const TSNode& node, GapBuffer* buffer, std::vector<Scope*>& scopeKing, Hash document)
{
	auto bigScope = &fileScope->file;

	#if _DEBUG
		auto start = ts_node_start_byte(node);
		auto end = ts_node_end_byte(node);
		bigScope->content = buffer_view(start, end, buffer);
	#endif

	bigScope->imperative = false;
	// uhhh just do all the imports now!

	auto named_count = ts_node_named_child_count(node);
	auto cursor = ts_tree_cursor_new(node);
	if (ts_tree_cursor_goto_first_child(&cursor))
	{
		//non empty tree:
		do
		{
			// this doesn't respect namespaces;
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

			// this doesn't respect non-top-level loads.
			// i think we also might need the path for this, unfortunately.
			if (ts_node_symbol(current) == g_constants.load)
			{
				auto nameNode = ts_node_named_child(current, 0);
				auto startOffset = ts_node_start_byte(nameNode) + 1;
				auto endOffset = ts_node_end_byte(nameNode) - 1;
				buffer_view view = buffer_view(startOffset, endOffset, buffer);
				// get the current directory, and append the load name to it.
				if (auto currentDirectory = g_filePaths.Read(document))
				{
					auto path = std::filesystem::path(currentDirectory.value());
					path.replace_filename(view.Copy());
					auto str = path.string();
					auto loadNameHash = StringHash(str);
					if (!g_filePaths.Read(loadNameHash))
					{
						g_filePaths.Write(loadNameHash, str);
					}

					fileScope->loads.push_back(loadNameHash);
				}
			}
		} while (ts_tree_cursor_goto_next_sibling(&cursor));
	}

	ts_tree_cursor_delete(&cursor);
	scopeKing.push_back(bigScope);
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

	return TokenType::Variable;
}


static void BuildFileScope(Hash documentHash)
{
	FileScope* fileScope;

	if (auto fileScopeOpt = g_fileScopes.Read(documentHash))
	{
		fileScope = fileScopeOpt.value();
	}
	else
	{
		fileScope = new FileScope();
		g_fileScopes.Write(documentHash, fileScope);
	}


	fileScope->imports.clear();
	fileScope->loads.clear();
	fileScope->scopes.clear();
	fileScope->tokens.clear();
	fileScope->file.declarations.clear();

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
		"(export_scope_directive) @export"
		"(file_scope_directive) @file"
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
		export_scope,
		file_scope,
	};

	std::vector<TSNode> parameters;
	std::vector<Scope*> scopeKing;
	std::vector<TSNode> unresolvedEntry;
	std::vector<int> unresolvedTokenIndex;

	bool skipNextIdentifier = false;
	Type* currentType = nullptr;
	bool exporting = true;

	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		ScopeMarker captureType = (ScopeMarker)match.captures[index].index;
		auto node = match.captures[index].node;
		switch (captureType)
		{
		case ScopeMarker::dataScope:
		{
			CreateScope(fileScope, node, buffer, parameters, currentType, scopeKing, false, documentHash);
			break;
		}
		case ScopeMarker::imperativeScope:
		{
			CreateScope(fileScope, node, buffer, parameters, currentType, scopeKing, true, documentHash);
			break;
		}
		case ScopeMarker::fileScope:
		{
			CreateTopLevelScope(fileScope, node, buffer, scopeKing, documentHash);
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

			DeclarationFlags flags = (DeclarationFlags)0;
			if (exporting)
				flags = DeclarationFlags::Exported;

			// todo fix this
			AddEntry(node, TokenType::Variable, fileScope, buffer, exprType, flags);
			break;
		}
		
		case ScopeMarker::const_decl:
		{
			skipNextIdentifier = true;

			DeclarationFlags flags = DeclarationFlags::Constant;
			if (exporting)
				flags = flags | DeclarationFlags::Exported;
			

			AddEntry(node, TokenType::Number, fileScope, buffer, nullptr, flags);
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
			type->documentHash = documentHash;

			DeclarationFlags flags = DeclarationFlags::Function;
			if (exporting)
				flags = flags | DeclarationFlags::Exported;

			AddEntry(node, TokenType::Function, fileScope, buffer, type, flags);
			break;
		}
		
		case ScopeMarker::struct_defn:
		{
			skipNextIdentifier = true;
			Type* type = new Type(); // leak this 
			type->name = GetIdentifierFromBuffer(node, buffer).CopyMalloc(); // leak this too!
			type->documentHash = documentHash;
			currentType = type;

			DeclarationFlags flags = DeclarationFlags::Struct;
			if (exporting)
				flags = flags | DeclarationFlags::Exported;

			AddEntry(node, TokenType::Type, fileScope, buffer, type, flags);
			break;
		}
		case ScopeMarker::enum_defn:
		{
			skipNextIdentifier = true;

			DeclarationFlags flags = DeclarationFlags::Enum;
			if (exporting)
				flags = flags | DeclarationFlags::Exported;

			AddEntry(node, TokenType::Enum, fileScope, buffer, nullptr, flags);
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
					if (frame->declarations.contains(hash))
					{
						token.type = GetTokenTypeFromFlags(frame->declarations[hash].flags);
						goto done;
					}
				}

				// search modules
				if (auto decl = fileScope->SearchModules(hash))
				{
					token.type = GetTokenTypeFromFlags(decl.value().flags);
					goto done;
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
		case ScopeMarker::export_scope:
		{
			exporting = true;
			break;
		}
		case ScopeMarker::file_scope:
		{
			exporting = false;
			break;
		}

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
			if (scope->declarations.contains(identifierHash))
			{
				// if the scope is imperative, then respect ordering, if it is not, just add the entry.
				if (scope->imperative)
				{
					auto definitionStart = scope->declarations[identifierHash].startByte;
					auto identifierStart = ts_node_start_byte(node);
					if (identifierStart >= definitionStart)
					{
						auto entry = scope->declarations[identifierHash];
						fileScope->tokens[index].type = GetTokenTypeFromFlags(entry.flags);
						foundInLocalScopes = true;
						break;
					}
				}
				else
				{
					auto entry = scope->declarations[identifierHash];
					fileScope->tokens[index].type = GetTokenTypeFromFlags(entry.flags);
					foundInLocalScopes = true;
					break;
				}
			}

			scope = GetScopeAndParentForNode(parent, fileScope, &parent); // aliasing???
		}

		if (foundInLocalScopes)
			continue;

		if( auto decl = fileScope->Search(identifierHash) )
		{
			fileScope->tokens[index].type = GetTokenTypeFromFlags(decl.value().flags);
		}
		else
		{
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

	if (auto treeOpt = g_trees.Read(documentHash))
	{
		ts_tree_delete(treeOpt.value());
	}

	TSParser* parser = ts_parser_new();
	ts_parser_set_language(parser, g_jaiLang);

	auto view = buffer->GetEntireStringView();
	auto tree = ts_parser_parse_string(parser, nullptr, view.data(), length);

	g_trees.Write(documentHash, tree);
	ts_parser_delete(parser);

	BuildFileScope(documentHash);
	// handle loads!
	auto fileScope = g_fileScopes.Read(documentHash).value();
	for (auto load : fileScope->loads)
	{
		HandleLoad(load);
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
