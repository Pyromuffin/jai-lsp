#include "FileScope.h"
#include <cassert>
#include <filesystem>

bool HandleLoad(Hash documentHash);

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
	return g_fileScopeByIndex.Read(handle.fileIndex)->GetType(handle);
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
	unresolvedTokenIndex.push_back((uint32_t)tokens.size());
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
		int index = (int)stack.scopes.size() - sp - 1;
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
	//unresolvedEntry.push_back(node);
	//unresolvedTokenIndex.push_back((uint32_t)tokens.size());
	token.type = (TokenType)-1;

done:
	tokens.push_back(token);
}


static ScopeDeclaration AddEntryToScope(TSNode node, const GapBuffer* buffer, Scope* scope, TypeHandle type, DeclarationFlags flags, TSNode rhs)
{
	ScopeDeclaration entry;
	auto start = ts_node_start_byte(node);
	entry.startByte = start;
	entry.flags = flags;
	auto end = ts_node_end_byte(node);
	entry.SetLength(end - start);

	if (flags & DeclarationFlags::Evaluated)
	{
		entry.type = type;
	}
	else
	{
		entry.SetRHSOffset(rhs.context[0] - start);
		entry.id = rhs.id;
	}

	
	scope->Add(GetIdentifierHash(node, buffer), entry);
	return entry;
}


void FileScope::HandleNamedDecl(const TSNode nameNode, ScopeHandle currentScope, std::vector<TSNode>& structs, bool exporting)
{
	auto cursor = Cursor();
	std::vector<TSNode> identifiers;
	TSNode rhs;
#if _DEBUG
	rhs = { 0 };
#endif

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

		rhs = cursor.Current();
	}
	else if (expressionType == g_constants.funcDefinition)
	{
		handle = HandleFuncDefinitionNode(node, currentScope, structs, flags);
	}
	else if (expressionType == g_constants.structDecl || expressionType == g_constants.unionDecl || expressionType == g_constants.enumDecl)
	{
		flags = flags | DeclarationFlags::Struct;
		flags = flags | DeclarationFlags::Evaluated;
		// descend into struct decl to find the scope.
		cursor.Child();
		while (cursor.Sibling())
		{
			auto scopeNode = cursor.Current();
			auto symbol = ts_node_symbol(scopeNode);
			if (symbol == g_constants.dataScope)
			{

				if (auto scopeHandle = GetScopeFromOffset(scopeNode.context[0], edits, editCount))
				{
					handle = GetScope(*scopeHandle)->associatedType;
				}
				else
				{
					handle = AllocateType();
					auto king = &types[handle.index];
					king->name = GetIdentifierFromBufferCopy(identifiers[0], buffer); //@todo this is probably not ideal, allocating a string for every type name every time.
					king->members = AllocateScope(scopeNode, currentScope);
					GetScope(king->members)->associatedType = handle;
					structs.push_back(scopeNode);
				}

				cursor.Sibling(); // skip "}"
			}
		}
	}
	else
	{
		// unresolved type?
		//auto hash = GetIdentifierHash(identifiers[0], buffer);
		//unresolvedTypes.push_back( std::make_tuple(hash, node));
		rhs = node;
	}


	for (auto identifier : identifiers)
	{
		AddEntryToScope(identifier, buffer, GetScope(currentScope), handle, flags, rhs);
	}


}

void FileScope::HandleUsingStatement(TSNode node, ScopeHandle scope, std::vector<TSNode>& structs, bool& exporting)
{
	auto child = ts_node_named_child(node, 0);
	if (ts_node_symbol(child) == g_constants.namedDecl)
	{
		HandleNamedDecl(child, scope, structs, exporting);
	}
	else
	{
		// do something here.
		//unresolvedTypes.push_back(std::make_tuple(Hash{ 0 }, node));
	}



}



void FileScope::FindDeclarations(TSNode scopeNode, ScopeHandle scope, ScopeStack& stack, bool& exporting, bool rebuild)
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

	std::vector<TSNode> structs;

	auto cursor = Cursor();
	cursor.Reset(scopeNode);

	if (ts_node_symbol(scopeNode) == g_constants.funcDefinition)
	{
		HandleFunctionDefnitionParameters(scopeNode, scope, cursor);
	}


	cursor.Child(); // inside the scope

	do
	{
		auto node = cursor.Current();
		auto type = ts_node_symbol(node);
		if (type == g_constants.namedDecl)
		{
			HandleNamedDecl(node, scope, structs, exporting);
		}

		else if (type == g_constants.usingStatement)
		{
			HandleUsingStatement(node, scope, structs, exporting);
		}

		else if (type == g_constants.imperativeScope)
		{
			// naked scope
			AllocateScope(node, scope);
			structs.push_back(node);
		}

		else if (type == g_constants.scopeExport)
			exporting = true;

		else if (type == g_constants.scopeFile)
			exporting = false;

	} while (cursor.Sibling());




	stack.scopes.push_back(scope);

	// when rebuilding, we only want to find declarations for scopes that don't exist yet!
	// i think we can find out which scopes don't exist from checking the offsets
	for (auto structNode : structs)
	{
		auto scopeHandle = GetScopeFromNodeID(structNode.id);
		FindDeclarations(structNode, scopeHandle, stack, exporting);
	}

	stack.scopes.pop_back();

}

void FileScope::HandleFunctionDefnitionParameters(TSNode node, ScopeHandle currentScope, Cursor& cursor) 
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
	auto typeHandle = GetScope(currentScope)->associatedType;
	auto king = &types[typeHandle.index];
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
				AddEntryToScope(identifierNode, buffer, GetScope(currentScope), TypeHandle::Null(), DeclarationFlags::None, rhsNode);
			}

			cursor.Parent();
		}

	}

	cursor.Parent(); // takes us to parameter list
	cursor.Sibling(); // hopefully takes us to imperative scope
}



TypeHandle FileScope::HandleFuncDefinitionNode(TSNode node, ScopeHandle currentScope, std::vector<TSNode>& structs, DeclarationFlags& flags)
{
	// so all we need to do here is allocate a type, find the scope node, add the type king info members and name. params get injected and resolved when we go to find the members in the scope later.

	flags = flags | DeclarationFlags::Function;
	flags = flags | DeclarationFlags::Evaluated;

	if (auto scopeHandle = GetScopeFromOffset(node.context[0], edits, editCount))
	{
		return GetScope(*scopeHandle)->associatedType;
	}

	Cursor& cursor = scope_builder_cursor; // please please dont put a function definition in here.
	cursor.Reset(node);

	cursor.Child(); // descend
	while (cursor.Sibling()); // scope is always the last node;
	auto scopeNode = cursor.Current();

	auto typeHandle = AllocateType();
	auto king = &types[typeHandle.index];
	auto scopeHandle = AllocateScope(node, currentScope);
	GetScope(scopeHandle)->associatedType = typeHandle;
	king->members = scopeHandle;
	structs.push_back(node); // not the scope node! this is how we can know we're doing a function definition later.

	return typeHandle;
}


static std::vector<const char*> builtins = {
		"bool",
		"float",
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

Module* RegisterModule(std::string moduleName, std::filesystem::path path);
std::optional<std::filesystem::path> ModuleFilePath(std::string name);

void FileScope::CreateTopLevelScope(TSNode node, ScopeStack& stack, bool& exporting)
{
	file = AllocateScope(node, {UINT16_MAX} );
	GetScope(file)->imperative = false;

	for (int i = 0; i < builtins.size(); i++)
	{
		auto handle = AllocateType();
		auto king = &types[handle.index];
		king->name = std::string(builtins[i]);
		ScopeDeclaration decl;
		decl.flags = DeclarationFlags::BuiltIn | DeclarationFlags::Evaluated;
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

				if (auto mod = g_modules.Read(moduleNameHash))
				{
					if(mod.value()->moduleFile->dirty)
						loadFutures.push_back( std::async(std::launch::async, HandleLoad, mod.value()->moduleFileHash));
				}
				else
				{
					auto moduleName = view.Copy();
					if (auto modulePath = ModuleFilePath(moduleName))
					{
						// then we need to create the module.
						auto mod = RegisterModule(moduleName, *modulePath);
						loadFutures.push_back(std::async(std::launch::async, HandleLoad, mod->moduleFileHash));
					}
				}
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

	for (auto& load : loads)
	{
		if (auto fileOpt = g_fileScopes.Read(documentHash))
		{
			auto file = fileOpt.value();
			if (!file->dirty)
				continue;
		}

		loadFutures.push_back(std::async(std::launch::async, HandleLoad, load));
	}


	stack.scopes.push_back(file);

	FindDeclarations(node, file, stack, exporting);

	ts_tree_cursor_delete(&cursor);
}

void FileScope::DoTypeCheckingAndInference(TSTree* tree)
{

	// so i think that we should be able to do this in order of scopes.
	for (auto& scope : scopeKings)
	{
		bool resolvedAtLeastOne = true;
		

		if (scope.size <= scope.small_size)
		{
			int unresolvedCount = scope.size;
			while (resolvedAtLeastOne && unresolvedCount > 0)
			{
				resolvedAtLeastOne = false;
				unresolvedCount = 0;

				for (int i = 0; i < scope.size; i++)
				{
					auto& decl = scope.small_declarations[i];

					if (decl.flags & DeclarationFlags::Evaluated)
					{
						continue;
					}

					TSNode node;
					node.id = decl.id;
					node.tree = tree;
					node.context[0] = decl.startByte + decl.GetRHSOffset();
					node.context[3] = 0;

					if (auto typeHandle = EvaluateNodeExpressionType(node, buffer, &scope))
					{
						decl.flags = decl.flags | DeclarationFlags::Evaluated;
						decl.type = *typeHandle;
					
						resolvedAtLeastOne = true;
					}
					else
					{
						unresolvedCount++;
					}
				}
			}
		}
		else
		{
			int unresolvedCount = scope.size;
			while (resolvedAtLeastOne && unresolvedCount > 0)
			{
				resolvedAtLeastOne = false;
				unresolvedCount = 0;
				// iterate through hash table
				for (auto& kvp : scope.declarations)
				{
					auto& decl = kvp.second;
					if (decl.flags & DeclarationFlags::Evaluated)
					{
						continue;
					}

					TSNode node;
					node.id = decl.id;
					node.tree = tree;
					node.context[0] = decl.startByte + decl.GetRHSOffset();
					node.context[3] = 0;

					if (auto typeHandle = EvaluateNodeExpressionType(node, buffer, &scope))
					{
						decl.flags = decl.flags | DeclarationFlags::Evaluated;
						decl.type = *typeHandle;

						resolvedAtLeastOne = true;
					}
					else
					{
						unresolvedCount++;
					}
				}
			}
		}
	}

/*
	bool resolvedAtLeastOne = false;

redo:
	// resolve types now that all declarations have been added, we should be able to infer everything.
	for (int i = 0; i < unresolvedNodes.size(); i++)
	{
		auto& unresolved = unresolvedNodes[i];
		auto node = std::get<1>(unresolved);
		auto hash = std::get<0>(unresolved);
		auto symbol = ts_node_symbol(node);
		bool usingStatement = hash.value == 0;
		if (usingStatement)
		{
			node = ts_node_named_child(node, 0);
		}

		auto exprType = EvaluateNodeExpressionType(node, buffer, scope, stack);
		if (exprType && *exprType != TypeHandle::Null())
		{

			if (usingStatement)
			{
				// defer these until we've solved everything else becuase we need fully populated scopes.
				usings.push_back(std::make_pair(scope, *exprType));
			}
			else
			{
				GetScope(scope)->UpdateType(hash, *exprType);
			}

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

	*/
}

void FileScope::WaitForDependencies()
{
	// this will deadlock if modules import themselves, which i *think* is allowed.
	// an alternative is to wait for the individual condition variables of each load
	// but that presents a possibility where we get to this point and the condition variables havent been created yet!
	for (auto& future : loadFutures)
	{
		future.wait();
		//auto result = future.get();
	}

	/*
	for (auto& loadHash : loads)
	{
		if (auto loadopt = g_fileScopes.Read(loadHash))
		{
			auto load = *loadopt;

			std::unique_lock<std::mutex> lock(load->declarationsFoundMutex);
			load->declarationsFoundCondition.wait(lock, [=] {return load->declarationsFound; });
		}
	}
	*/

	// i think this is ok!? however, recursive loads will deadlock.
	/*
	{
		std::lock_guard lock{ declarationsFoundMutex };
		declarationsFound = true;
	}
	declarationsFoundCondition.notify_all();


	for (auto& import : imports)
	{
		if(auto modopt = g_modules.Read(import))
		{ 
			auto mod = *modopt;
			auto file = mod->moduleFile;

			std::unique_lock<std::mutex> lock(file->declarationsFoundMutex);
			file->declarationsFoundCondition.wait(lock, [=] {return file->declarationsFound; });

		}
	}
	*/
}




void FileScope::Build()
{
	if (!dirty)
		return;

	Clear();

	buffer = g_buffers.Read(documentHash).value();
	auto tree = g_trees.Read(documentHash).value();
	auto root = ts_tree_root_node(tree);

	ScopeStack stack;

	bool exporting = true;
	bool skipNextImperative = false;

	CreateTopLevelScope(root, stack, exporting);

	dirty = false;
}

void FileScope::DoTokens2()
{
	auto tree = g_trees.Read(documentHash).value();
	auto root = ts_tree_root_node(tree);

	WaitForDependencies();

	DoTypeCheckingAndInference(tree);

	for (auto& use : usings)
	{
		auto type = GetType(use.second);
		auto memberScope = GetScope(type->members);
		memberScope->InjectMembersTo(GetScope(use.first));
	}
	usings.clear();



	ScopeStack stack;
	stack.scopes.push_back(file);
	std::vector<TSNode> unresolvedEntry;
	std::vector<int> unresolvedTokenIndex;

	bool exporting = true;
	bool skipNextImperative = false;

	static const auto queryText =
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
		(uint32_t)strlen(queryText),
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

	int identifiersToSkip = 0;

	Cursor functionBodyFinder;
	int skipPopCount = 0;

	offsetToHandle.Clear();
	offsetToHandle.Add(2, file);

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
			offsetToHandle.Add(ts_node_start_byte(node), scopeHandle);
			break;
		}
		case ScopeMarker::func_defn:
		{
			skipNextImperative = true;

			if (!ContainsScope(node.id)) // likely some error state
			{
				skipPopCount++;
				break;
			}

			auto scopeHandle = GetScopeFromNodeID(node.id);
			stack.scopes.push_back(scopeHandle);

			offsetToHandle.Add(ts_node_start_byte(node), scopeHandle);
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
				skipPopCount++;
				break;
				/*
				// scope in an if statement or something probably.
				auto scopeHandle = AllocateScope(node, stack.scopes.back());
				FindDeclarations(node, scopeHandle, stack, exporting);
				for (auto& use : usings)
				{
					auto type = GetType(use.second);
					auto memberScope = GetScope(type->members);
					memberScope->InjectMembersTo(GetScope(use.first));
				}
				usings.clear();
				*/
			}

			auto scopeHandle = GetScopeFromNodeID(node.id);
			stack.scopes.push_back(scopeHandle);

			offsetToHandle.Add(ts_node_start_byte(node), scopeHandle);
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



void FileScope::DoTokens(TSNode root, TSInputEdit* edits, int editCount)
{
	const static auto queryText =
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
		(uint32_t)strlen(queryText),
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
	stack.scopes.push_back(file);
	std::vector<TSNode> unresolvedEntry;
	std::vector<int> unresolvedTokenIndex;

	bool exporting = true;
	bool skipNextImperative = false;
	int identifiersToSkip = 0;
	int skipPopCount = 0;

	Hashmap newOffsets;
	newOffsets.Add(2, file); // why is this 2? i don't know.
	tokens.clear();

	while (ts_query_cursor_next_capture(queryCursor, &match, &index))
	{
		ScopeMarker captureType = (ScopeMarker)match.captures[index].index;
		auto node = match.captures[index].node;
		switch (captureType)
		{
		
		
		case ScopeMarker::func_defn:
			skipNextImperative = true;
			[[fallthrough]];
		case ScopeMarker::dataScope:
		{
			auto scopeHandle = GetScopeFromOffset(node.context[0], edits, editCount);
			scopeHandle = scopeHandle ? scopeHandle : TryGetScopeFromNodeID(node.id);

			if(!scopeHandle)
			{
				skipPopCount++;
				break;
			}

			stack.scopes.push_back(*scopeHandle);
			newOffsets.Add(node.context[0], *scopeHandle);
			SetScopePresentBit(*scopeHandle);
			
			break;
		}
		case ScopeMarker::imperativeScope:
		{
			if (skipNextImperative)
			{
				skipNextImperative = false;
				break;
			}

			auto scopeHandle = GetScopeFromOffset(node.context[0], edits, editCount);

			if (!scopeHandle)
			{
				// naked scope
				scopeHandle = AllocateScope(node, stack.scopes.back());
				FindDeclarations(node, *scopeHandle, stack, exporting);
			}

			stack.scopes.push_back(*scopeHandle);
			newOffsets.Add(node.context[0], *scopeHandle);
			SetScopePresentBit(*scopeHandle);
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


	offsetToHandle.Clear();
	offsetToHandle = newOffsets;

	ts_query_delete(query);
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
			int index = (int)stack.scopes.size() - sp - 1;
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

const std::optional<TypeHandle> FileScope::EvaluateNodeExpressionType(TSNode node, const GapBuffer* buffer, Scope* scope)
{
	auto symbol = ts_node_symbol(node);
	auto hash = GetIdentifierHash(node, buffer);

	if (symbol == g_constants.identifier)
	{
		// identifier of some kind. struct, union, or enum, or also a built in type now. we don't treat those differently than identifiers.

		while (scope)
		{
			if (auto decl = scope->TryGet(hash))
			{
				if (decl->flags & DeclarationFlags::Evaluated)
				{
					return decl->type;
				}
				else
				{
					return std::nullopt;
				}
			}

			scope = GetScope(scope->parent);
		}
		

		if (auto decl = SearchModules(hash))
		{
			if (decl->flags & DeclarationFlags::Evaluated)
			{
				return decl->type;
			}
			else
			{
				return std::nullopt;
			}
		}
	}

	return std::nullopt;
}



void FileScope::RebuildScope(TSNode newScopeNode, TSInputEdit* edits, int editCount, TSNode root)
{
	this->edits = edits;
	this->editCount = editCount;

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

	if (ts_node_is_null(newScopeNode))
	{
		// this is likely the file scope then.
		newScopeNode = root;
	}

	int startByte = ts_node_start_byte(newScopeNode);

	// fix up scope hashtable
	//FixUpHashmapOffsets(offsetToHandle, edits, editCount);

	auto uneditedStart = startByte;
	for (int i = 0; i < editCount; i++)
	{
		uneditedStart = UnEditStartByte(uneditedStart, edits[i].start_byte, edits[i].new_end_byte, edits[i].old_end_byte);
	}

	if (uneditedStart == 0)
	{
		// then this is the file scope, probably.
	}

	// first find which node the newScopeNode corresponds to by correlating it with the old node by start-byte
	auto scopeIndex = offsetToHandle.GetIndex(uneditedStart);

	if (scopeIndex != -1)
	{
		auto handle = offsetToHandle[scopeIndex];

		// yay now rebuild the scope somehow!

		// first generate the scope stack
		ScopeStack stack;

		TSNode newNode = newScopeNode;
		newNode = ts_node_parent(newNode);

		while (!ts_node_is_null(newNode))
		{
			auto index = offsetToHandle.GetIndex(ts_node_start_byte(newNode));
			if (index != -1)
			{
				auto handle = offsetToHandle[index];
				stack.scopes.push_back(handle);
			}

			newNode = ts_node_parent(newNode);
		}

		std::reverse(stack.scopes.begin(), stack.scopes.end());

		GetScope(handle)->Clear();
		bool exporting;

		_nodeToScopes.clear();
		FindDeclarations(newScopeNode, handle, stack, exporting, true);
		
		ClearScopePresentBits();
		DoTokens(root, edits, editCount);

		// now iterate through the bitmaps and free old scopes.
		auto& current = scopePresentBitmap[whichBitmap];
		auto& previous = scopePresentBitmap[(whichBitmap + 1) % 2];

		for (int i = 0; i < scopePresentBitmap[0].size(); i++)
		{
			auto bits = ~previous[i] & current[i];

			while (bits > 0)
			{
				unsigned long firstHigh;
				_BitScanReverse64(&firstHigh, bits);

				// free scope.
				auto scopeIndex = (i * 64) + firstHigh;
				scopeKingFreeList.push_back({ (uint16_t)scopeIndex });
				bits &= ~(1 << firstHigh); // clear bit for current scope
			}
		}


	}
}


