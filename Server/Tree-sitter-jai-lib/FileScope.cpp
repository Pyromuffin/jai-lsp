
#include "FileScope.h"
#include <cassert>
#include <filesystem>

std::optional<ScopeDeclaration> FileScope::SearchExports(Hash identifierHash)
{
	if (auto decl = GetScope(file)->TryGet(identifierHash))
	{
		if (decl.value().flags & DeclarationFlags::Exported)
		{
			return decl;
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

std::optional<ScopeDeclaration> FileScope::SearchModules(Hash identifierHash)
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

std::optional<ScopeDeclaration> FileScope::Search(Hash identifierHash)
{
	if (auto decl = GetScope(file)->TryGet(identifierHash))
	{
		return decl;
	}

	return SearchModules(identifierHash);
}

static const TypeKing* GetGlobalType(TypeHandle handle)
{
	return g_fileScopeByIndex[handle.fileIndex]->GetType(handle);
}

void FileScope::HandleMemberReference(TSNode rhsNode, ScopeStack& stack, std::vector<TSNode>& unresolvedEntry, std::vector<int>& unresolvedTokenIndex)
{
	auto rhsHash = GetIdentifierHash(rhsNode, buffer);

	auto start = ts_node_start_point(rhsNode);
	auto end = ts_node_end_point(rhsNode);
	SemanticToken token;
	token.col = start.column;
	token.line = start.row;
	token.length = end.column - start.column;
	token.modifier = (TokenModifier)0;

	auto decl = GetDeclarationForNode(rhsNode, this, buffer); // this should probably use the scope stack for better performance
	if (decl)
	{
		token.type = GetTokenTypeFromFlags(decl->flags);
		tokens.push_back(token);
		return;
	}

	// if we get here then we've got an unresolved identifier, that we will check when we've parsed the entire document.
	unresolvedEntry.push_back(rhsNode);
	unresolvedTokenIndex.push_back(tokens.size());
	token.type = (TokenType)-1;

	tokens.push_back(token);
	return;

}



void FileScope::HandleVariableReference(TSNode node, ScopeStack& stack, std::vector<TSNode>& unresolvedEntry, std::vector<int>& unresolvedTokenIndex)
{
	auto hash = GetIdentifierHash(node, buffer);

	auto start = ts_node_start_point(node);
	auto end = ts_node_end_point(node);
	SemanticToken token;
	token.col = start.column;
	token.line = start.row;
	token.length = end.column - start.column;
	token.modifier = (TokenModifier)0;


	for (int sp = 0; sp < stack.scopes.size(); sp++)
	{
		int index = stack.scopes.size() - sp - 1;
		auto frame = stack.scopes[index];
		if (auto decl = GetScope(frame)->TryGet(hash))
		{
			token.type = GetTokenTypeFromFlags(decl->flags);
			goto done;
		}
	}

	// search modules
	if (auto decl = SearchModules(hash))
	{
		token.type = GetTokenTypeFromFlags(decl->flags);
		goto done;
	}

	// if we get here then we've got an unresolved identifier, that we will check when we've parsed the entire document.
	unresolvedEntry.push_back(node);
	unresolvedTokenIndex.push_back(tokens.size());
	token.type = (TokenType)-1;

done:
	tokens.push_back(token);
}


static ScopeDeclaration AddEntryToScope(TSNode node, const GapBuffer* buffer, Scope* scope, TypeHandle type, DeclarationFlags flags)
{
	ScopeDeclaration entry;
	entry.type = type;
	entry.flags = flags;
	auto start = ts_node_start_byte(node);
	auto end = ts_node_end_byte(node);

	entry.startByte = start;
	entry.length = end - start;

	scope->Add(GetIdentifierHash(node, buffer), entry);
	return entry;
}




void FileScope::HandleNamedDecl(const TSNode nameNode, ScopeHandle currentScope, std::vector<std::tuple<Hash, TSNode>>& unresolvedTypes, std::vector<std::pair<TSNode, TypeHandle>>& structs, bool exporting)
{
	auto cursor = Cursor();
	std::vector<TSNode> identifiers;

	identifiers.clear();
	cursor.Reset(nameNode);

	// a bunch of things going on here
	// lets get the names first.
	cursor.Child(); // names node
	cursor.Child(); // first name expression


	do
	{
		auto node = cursor.Current();
		if (ts_node_symbol(node) == g_constants.identifier)
		{
			identifiers.push_back(node);
		}

	} while (cursor.Sibling());

	if (identifiers.empty())
	{
		// likely some kind of error, or more complicated expression in the "names" node.
		return;
	}

	cursor.Parent(); // back to names node
	cursor.Sibling(); // always a ":"
	cursor.Sibling(); // this is the hard part lol
	auto node = cursor.Current();

	// for block expressions eg structs and functions, the next node is not named. Usually.
	if (!ts_node_is_named(node))
	{
		cursor.Sibling();
		node = cursor.Current();
	}

	auto expressionType = ts_node_symbol(node);
	// make these symbols constant somehow to use a switch statement
	DeclarationFlags flags = (DeclarationFlags)0;
	TypeHandle handle = TypeHandle::Null();

	if (exporting)
		flags = flags | DeclarationFlags::Exported;

	if (expressionType == g_constants.constDecl)
	{
		//@TODO doesn't handle compound declarations.
		flags = flags | DeclarationFlags::Constant;
		auto hash = GetIdentifierHash(identifiers[0], buffer);

		// descend into initializer
		// from the grammar:
		/*
		const_initializer: $ => seq(
			optional($._expression), 
			seq(
			":", 
			CommaSep1($._expression)),
		),
		*/
		cursor.Child(); // first expr, or ":"
		if (!ts_node_is_named(cursor.Current()))
		{
			// if we get here then we hit ":", so it's an implicit initializer
			cursor.Sibling();
		}

		unresolvedTypes.push_back(std::make_tuple(hash, cursor.Current()));
	}
	else if (expressionType == g_constants.funcDefinition)
	{
		handle = HandleFuncDefinitionNode(node, structs, flags);
	}
	else if (expressionType == g_constants.structDecl || expressionType == g_constants.unionDecl || expressionType == g_constants.enumDecl)
	{
		// allocate a new typeking
		handle = AllocateType();
		auto king = &types[handle.index];

		flags = flags | DeclarationFlags::Struct;
		// descend into struct decl to find the scope.
		cursor.Child();
		while (cursor.Sibling())
		{
			auto scopeNode = cursor.Current();
			auto symbol = ts_node_symbol(scopeNode);
			if (symbol == g_constants.dataScope)
			{
				king->name = GetIdentifierFromBufferCopy(identifiers[0], buffer); //@todo this is probably not ideal, allocating a string for every type name every time.
				king->members = AllocateScope(scopeNode);
				GetScope(king->members)->associatedType = handle;
				structs.push_back(std::make_pair(scopeNode, handle));

				cursor.Sibling(); // skip "}"
			}

		}
	}
	else
	{
		// unresolved type?
		auto hash = GetIdentifierHash(identifiers[0], buffer);
		unresolvedTypes.push_back( std::make_tuple(hash, node));
	}


	for (auto identifier : identifiers)
	{
		AddEntryToScope(identifier, buffer, GetScope(currentScope), handle, flags);
	}


}


void FileScope::FindDeclarations(TSNode scopeNode, ScopeHandle scope, TypeHandle handle, ScopeStack& stack, bool& exporting, bool rebuild)
{
	// OK TO MAKE THIS WORK we need to handle the following two cases:
	/*

		myThing : Thing1;
		myThing.item1;     <--------------- Problem 1) myThing needs to know about the members on Thing1 before it gets to them.

		Thing1 :: struct
		{
		   item1 : Thing2; <--------------- Problem 2) Thing1 needs to know that Thing2 exists. If we create the scope of Thing1 when we encounter it, then we can't know about Thing2.
		}

		Thing2 :: struct
		{
			name : string;
		}
	*/
	// so we can do breadth first, finding declarations, then doing type-resovling-and-inference, this solves problem 2.
	// we can try the following:
	// do all the declarations in a scope, breadth first
	// then do all the scopes in the scope, breadth first. this is annoying becuase now we have to iterate through the tree 3 times. Or we can keep track of the struct bodies that need scope-creating.
	// then do the identifier reference colorings in order

	GetScope(scope)->node = scopeNode;
	auto parent = ts_node_parent(scopeNode);

	std::vector<std::pair<TSNode, TypeHandle>> structs;
	std::vector<std::tuple<Hash, TSNode>> unresolvedNodes;
	unresolvedNodes.clear();

	auto cursor = Cursor();
	cursor.Reset(scopeNode);

	if (ts_node_symbol(scopeNode) == g_constants.funcDefinition)
	{
		HandleFunctionDefnitionParameters(scopeNode, scope, handle, unresolvedNodes, cursor);
	}


	cursor.Child(); // inside the scope

	do
	{
		auto node = cursor.Current();
		auto type = ts_node_symbol(node);
		if (type == g_constants.namedDecl)
		{
			HandleNamedDecl(node, scope, unresolvedNodes, structs, exporting);
		}

		if (type == g_constants.usingStatement)
		{
			node = ts_node_child(node, 0);
			if(ts_node_symbol(node) == g_constants.namedDecl)
				HandleNamedDecl(node, scope, unresolvedNodes, structs, exporting);
		}

		else if (type == g_constants.scopeExport)
			exporting = true;

		else if (type == g_constants.scopeFile)
			exporting = false;

	} while (cursor.Sibling());


	bool resolvedAtLeastOne = false;

redo:
	// resolve types now that all declarations have been added, we should be able to infer everything.
	for (int i = 0; i < unresolvedNodes.size(); i++)
	{
		auto& unresolved = unresolvedNodes[i];
		auto exprType = EvaluateNodeExpressionType(std::get<1>(unresolved), buffer, scope, stack);
		if (exprType && exprType.value() != TypeHandle::Null())
		{
			auto handle = exprType.value();
			GetScope(scope)->UpdateType(std::get<0>(unresolved), handle);

			// erase swap back.
			unresolved = unresolvedNodes.back();
			unresolvedNodes.pop_back();
			i--;

			resolvedAtLeastOne = true;
		}
	}

	if (resolvedAtLeastOne && !unresolvedNodes.empty())
	{
		resolvedAtLeastOne = false;
		goto redo;
	}


	if (!rebuild)
	{
		stack.scopes.push_back(scope);

		// after types have been inferred, we can go into the struct bodies and create their members
		for (auto pair : structs)
		{
			// find declarations in scopes

			// this is actually a happy accident. we don't really want to make the function definition node "own" the scope, but maybe thats ok because it saves us from having find the imperative scope on the function later when we're creating tokens.
			auto scopeHandle = GetScopeFromNodeID(pair.first.id);
			FindDeclarations(pair.first, scopeHandle, pair.second, stack, exporting);
		}

		stack.scopes.pop_back();
	}



}

void FileScope::HandleFunctionDefnitionParameters(TSNode node, ScopeHandle currentScope, TypeHandle handle, std::vector<std::tuple<Hash, TSNode>>& unresolvedNodes, Cursor& cursor) 
{
	// by the time we get here, all outside declarations should have been added.
	// grab parameters, and imperative scope,
	// inject parameters into scope.


	/*
	function_definition : $ => prec(1, seq(
        optional("inline"),
        $.function_header,
        $.imperative_scope,
      )),
	*/
	
	cursor.Child(); // parameter list or inline 
	
	if (!ts_node_is_named(cursor.Current()))
		cursor.Sibling(); //skip over "inline" node

	// now the cursor should be at the function header.
	// we want to extract the parameters from the function header.
	auto headerNode = cursor.Current();
	auto king = &types[handle.index];
	king->name = GetIdentifierFromBufferCopy(headerNode, buffer);

	cursor.Child(); //inside function header, should be parameter list

	/*
	 function_header : $ => prec.left(seq(
        $._parameter_list,
        optional($.trailing_return_types),
        repeat($.trailing_directive)
      )),*/

	cursor.Child(); //inside parameter list

	/*
	_parameter_list: $ => seq(
        '(',
          CommaSep($.parameter),
        ')'
      ),
	*/


	// so parameters do type checking in the outer scope, but get injected into the inner scope.
	// 

	while (cursor.Sibling()) // first call skips the "("
	{
		auto parameterNode = cursor.Current();
		if (ts_node_symbol(parameterNode) == g_constants.parameter)
		{
			king->parameters.push_back(GetIdentifierFromBufferCopy(parameterNode, buffer));

			//get identifier
			cursor.Child();
			auto identifierNode = cursor.Current();
			if (cursor.Sibling()) // always ':', probably. it may be possible to have a weird thing here where we dont have an rhs
			{
				cursor.Sibling(); // rhs expression, could be expression, variable initializer single, const initializer single
				auto rhsNode = cursor.Current();

				auto hash = GetIdentifierHash(identifierNode, buffer);

				unresolvedNodes.push_back(std::make_tuple(hash, rhsNode));
				AddEntryToScope(identifierNode, buffer, GetScope(currentScope), TypeHandle::Null(), DeclarationFlags::None);
			}

			cursor.Parent();
		}

	}

	cursor.Parent(); // takes us to parameter list
	cursor.Sibling(); // hopefully takes us to imperative scope
}



TypeHandle FileScope::HandleFuncDefinitionNode(TSNode node, std::vector<std::pair<TSNode, TypeHandle>>& structs, DeclarationFlags& flags)
{
	// so all we need to do here is allocate a type, find the scope node, add the type king info members and name. params get injected and resolved when we go to find the members in the scope later.

	flags = flags | DeclarationFlags::Function;


	Cursor& cursor = scope_builder_cursor; // please please dont put a function definition in here.
	cursor.Reset(node);

	cursor.Child(); // descend
	while (cursor.Sibling()); // scope is always the last node;
	auto scopeNode = cursor.Current();

	auto typeHandle = AllocateType();
	auto king = &types[typeHandle.index];
	auto scopeHandle = AllocateScope(node);
	GetScope(scopeHandle)->associatedType = typeHandle;
	king->members = scopeHandle;
	structs.push_back(std::make_pair(node, typeHandle)); // not the scope node! this is how we can know we're doing a function definition later.

	return typeHandle;
}


static const char* builtins[] = {
		"bool",
		"float32",
		"float64",
		"char",
		"string",
		"s8",
		"s16",
		"s32",
		"s64",
		"int",
		"u8",
		"u16",
		"u32",
		"u64",
		"void"
};


void FileScope::CreateTopLevelScope(TSNode node, ScopeStack& stack, bool& exporting)
{
	file = AllocateScope(node);
	GetScope(file)->imperative = false;

	for (int i = 0; i < 15; i++)
	{
		auto handle = AllocateType();
		auto king = &types[handle.index];
		king->name = std::string(builtins[i]);
		ScopeDeclaration decl;
		decl.flags = DeclarationFlags::BuiltIn;
		decl.type = handle;
		GetScope(file)->Add(StringHash(builtins[i]), decl);
	}


	// uhhh just do all the imports now!
	//auto named_count = ts_node_named_child_count(node);
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
				imports.push_back(moduleNameHash);
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
				if (auto currentDirectory = g_filePaths.Read(documentHash))
				{
					auto path = std::filesystem::path(currentDirectory.value());
					path.replace_filename(view.Copy());
					auto str = path.string();
					auto loadNameHash = StringHash(str);
					if (!g_filePaths.Read(loadNameHash))
					{
						g_filePaths.Write(loadNameHash, str);
					}

					loads.push_back(loadNameHash);
				}
			}
		} while (ts_tree_cursor_goto_next_sibling(&cursor));
	}

	stack.scopes.push_back(file);

	FindDeclarations(node, file, TypeHandle::Null(), stack, exporting);

	ts_tree_cursor_delete(&cursor);
}


void FileScope::Build()
{
	Clear();

	buffer = g_buffers.Read(documentHash).value();
	auto tree = g_trees.Read(documentHash).value();
	auto root = ts_tree_root_node(tree);


	auto queryText =
		"(function_definition) @func_defn" // hopefully this matches before the identifiers does.
		"(imperative_scope) @imperative.scope"
		"(data_scope) @data.scope"
		"(member_access . (_) (_) @member_rhs )"
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
		func_defn,
		imperativeScope,
		dataScope,
		member_rhs,
		var_ref,
		block_end,
		export_scope,
		file_scope,
	};

	ScopeStack stack;
	std::vector<TSNode> unresolvedEntry;
	std::vector<int> unresolvedTokenIndex;

	bool exporting = true;
	bool skipNextImperative = false;

	CreateTopLevelScope(root, stack, exporting);

	int identifiersToSkip = 0;

	Cursor functionBodyFinder;
	int skipPopCount = 0;

	scopeOffsets.clear();

	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		ScopeMarker captureType = (ScopeMarker)match.captures[index].index;
		auto node = match.captures[index].node;
		switch (captureType)
		{
		case ScopeMarker::dataScope:
		{

			if (!ContainsScope(node.id))
			{
				skipPopCount++;
				break;
			}

			auto scopeHandle = GetScopeFromNodeID(node.id);
			stack.scopes.push_back(scopeHandle);

			// also build up the list of scope contexts
			scopeOffsets.push_back(ts_node_start_byte(node));
			scopeOffsetHandles.push_back(scopeHandle);
			break;
		}
		case ScopeMarker::func_defn:
		{
			skipNextImperative = true;
			auto scopeHandle = GetScopeFromNodeID(node.id);
			stack.scopes.push_back(scopeHandle);

			scopeOffsets.push_back(ts_node_start_byte(node));
			scopeOffsetHandles.push_back(scopeHandle);
			break;
		}
		case ScopeMarker::imperativeScope:
		{
			if (skipNextImperative)
			{
				skipNextImperative = false;
				break;
			}

			if (!ContainsScope(node.id))
			{
				// naked scope
				auto scopeHandle = AllocateScope(node);
				FindDeclarations(node, scopeHandle, TypeHandle::Null(), stack, exporting);
			}
	
			auto scopeHandle = GetScopeFromNodeID(node.id);
			stack.scopes.push_back(scopeHandle);
			scopeOffsets.push_back(ts_node_start_byte(node));
			scopeOffsetHandles.push_back(scopeHandle);
			break;
		}
		case ScopeMarker::member_rhs:
		{
			HandleMemberReference(node, stack, unresolvedEntry, unresolvedTokenIndex);
			identifiersToSkip++;
			break;
		}
		case ScopeMarker::var_ref:
		{
			if (identifiersToSkip > 0)
			{
				identifiersToSkip--;
				break;
			}

			HandleVariableReference(node, stack, unresolvedEntry, unresolvedTokenIndex);
			break;
		}
		case ScopeMarker::block_end:
		{
			if (skipPopCount > 0)
			{
				skipPopCount--;
				break;
			}

			stack.scopes.pop_back();
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

	ts_query_delete(query);
	ts_query_cursor_delete(queryCursor);
}



const std::optional<TypeHandle> FileScope::EvaluateNodeExpressionType(TSNode node, const GapBuffer* buffer, ScopeHandle current, ScopeStack& stack)
{
	auto symbol = ts_node_symbol(node);
	auto hash = GetIdentifierHash(node, buffer);

	/*
	if (symbol == g_constants.builtInType)
	{
		// built in types need to get addded to the global type king holder.
		//return g_constants.builtInTypes[hash];
	}
	*/
	if (symbol == g_constants.identifier)
	{
		// identifier of some kind. struct, union, or enum, or also a built in type now. we don't treat those differently than identifiers.

		if (auto decl = GetScope(current)->TryGet(hash))
		{
			return decl.value().type;
		}

		for (int sp = 0; sp < stack.scopes.size(); sp++)
		{
			int index = stack.scopes.size() - sp - 1;
			auto frame = stack.scopes[index];
			if (auto decl = GetScope(frame)->TryGet(hash))
			{
				return decl.value().type;
			}
		}
		

		if (auto decl = Search(hash))
		{
			return decl.value().type;
		}
	}

	// unresolved type or expression of some kind.

	return std::nullopt;
}

void FileScope::RebuildScope(TSNode newScopeNode, TSInputEdit* edits, int editCount)
{
	// first first find the scope node we are within
	while (!ts_node_is_null(newScopeNode))
	{
		auto symbol = ts_node_symbol(newScopeNode);

		if (symbol == g_constants.dataScope)
		{
			break;
		}
		else if (symbol == g_constants.imperativeScope)
		{
			auto parent = ts_node_parent(newScopeNode);
			auto parentSymbol = ts_node_symbol(parent);
			if (parentSymbol == g_constants.funcDefinition)
			{
				newScopeNode = parent;
			}

			break;
		}

		newScopeNode = ts_node_parent(newScopeNode);
	}



	// first find which node the newScopeNode corresponds to by correlating it with the old node by start-byte
	int startByte = ts_node_start_byte(newScopeNode);
	auto scopeIndex = SearchScopeOffsets(startByte, edits[0]);

	if (scopeIndex != -1)
	{
		auto handle = scopeOffsetHandles[scopeIndex];

		// yay now rebuild the scope somehow!

		// first generate the scope stack
		ScopeStack stack;


		auto node = GetScope(handle)->node;
		auto oldNode = node;
		std::vector<const void*> oldNodes;
		std::vector<TSNode> newNodes;
		TSNode newNode = newScopeNode;

		//ts_node_edit(&node, &edits[0]);
		auto root = ts_tree_root_node(node.tree);
		auto newRoot = ts_tree_root_node(newNode.tree);

		auto nodeString = DebugNode(node, buffer);
		auto newNodeString = DebugNode(newNode, buffer);
		auto nodeRootString = DebugNode(root, buffer);
		auto newNodeRootString = DebugNode(newRoot, buffer);

		auto nodeSyntax = ts_node_string(node);
		auto newNodeSyntax = ts_node_string(newNode);
		auto rootSyntax = ts_node_string(root);
		auto newRootSyntax = ts_node_string(newRoot);

		node = ts_node_parent(node);
		newNode = ts_node_parent(newNode);

		auto nodeString2 = DebugNode(node, buffer);
		auto newNodeString2 = DebugNode(newNode, buffer);
		auto nodeSyntax2 = ts_node_string(node);
		auto newNodeSyntax2 = ts_node_string(newNode);

		while (!ts_node_is_null(node))
		{
			if (ContainsScope(node.id))
			{
				auto handle = GetScopeFromNodeID(node.id);
				stack.scopes.push_back(handle);
				oldNodes.push_back(node.id);
				newNodes.push_back(newNode);
			}

			newNode = ts_node_parent(newNode);
			node = ts_node_parent(node);
		}

		std::reverse(stack.scopes.begin(), stack.scopes.end());

		GetScope(handle)->Clear();
		bool exporting;

		FindDeclarations(newScopeNode, handle, GetScope(handle)->associatedType, stack, exporting, true);

		// now edit the nodes in the hash map going up.
		_nodeToScopes.erase(oldNode.id);
		_nodeToScopes.insert(std::make_pair(newScopeNode.id, handle));
		
		for (int i = 0; i < oldNodes.size(); i ++)
		{
			auto reversedIndex = oldNodes.size() - i - 1;
			auto newHandle = stack.scopes[reversedIndex];
			_nodeToScopes.erase(oldNodes[i]);
			_nodeToScopes.insert(std::make_pair(newNodes[i].id, newHandle));
			GetScope(newHandle)->node = newNodes[i];
		}


		// now iterate through the bitmaps and free old scopes.

	}
}


