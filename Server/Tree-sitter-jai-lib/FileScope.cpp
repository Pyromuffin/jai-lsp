#include "FileScope.h"
#include <cassert>
#include <filesystem>

TypeHandle FileScope::intType;
TypeHandle FileScope::stringType;
TypeHandle FileScope::floatType;
TypeHandle FileScope::emptyType;

Scope* FileScope::builtInScope;
Hash FileScope::preloadHash;

bool HandleLoad(Hash documentHash);
Module* RegisterModule(std::string moduleName, std::filesystem::path path);
std::optional<std::filesystem::path> FindModuleFilePath(std::string name);
std::optional<TypeHandle> EvaluateMemberAccessType(TSNode node, FileScope* fileScope, Scope* scope);


TSNode ConstructRhsFromDecl(ScopeDeclaration decl, TSTree* tree)
{
	assert((decl.flags & DeclarationFlags::Evaluated) == 0);
	TSNode node;
	node.id = decl.id;
	node.tree = tree;
	node.context[0] = decl.startByte + decl.GetRHSOffset();
	node.context[3] = 0;

	return node;
}


std::optional<ScopeDeclaration> FileScope::TryGet(Hash identifierHash)
{
	if (auto decl = GetScope(file)->TryGet(identifierHash))
	{
		if (decl.value().flags & DeclarationFlags::Exported)
		{
			return decl;
		}

	}

	return std::nullopt;
}

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

int FileScope::SearchAndGetExport(Hash identifierHash, FileScope** outFile, Scope** outDeclScope)
{
	auto declScope = GetScope(file);
	auto declIndex = declScope->GetIndex(identifierHash);
	if (declIndex >= 0)
	{
		auto data = declScope->declarations.Data();
		auto decl = &data[declIndex].value;
		if (decl->flags & DeclarationFlags::Exported)
		{
			*outDeclScope = declScope;
			*outFile = this;
			return declIndex;
		}

	}

	for (auto loadHash : loads)
	{
		if (auto file = g_fileScopes.Read(loadHash))
		{
			auto declIndex = file.value()->SearchAndGetExport(identifierHash, outFile, outDeclScope);
			if (declIndex >= 0)
			{
				return declIndex;
			}
		}
	}

	return -1;
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

int FileScope::SearchAndGetModule(Hash identifierHash, FileScope** outFile, Scope** declScope)
{
	for (auto modHash : imports)
	{
		if (auto mod = g_modules.Read(modHash))
		{
			auto declIndex = mod.value()->SearchAndGetFile(identifierHash, outFile, declScope);
			if (declIndex >= 0)
			{
				return declIndex;
			}
		}
	}

	for (auto loadHash : loads)
	{
		if (auto file = g_fileScopes.Read(loadHash))
		{
			auto declIndex = file.value()->SearchAndGetExport(identifierHash, outFile, declScope);
			if (declIndex >= 0)
			{
				return declIndex;
			}
		}
	}

	return -1;
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



static ScopeDeclaration AddUsingToScope(TSNode node, const GapBuffer* buffer, Scope* scope, TypeHandle type, DeclarationFlags flags)
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
		entry.flags = entry.flags | DeclarationFlags::Expression;
		entry.SetRHSOffset(0);
		entry.id = node.id;
	}


	scope->Add(GetIdentifierHash(node, buffer), entry);
	return entry;
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

void FileScope::HandleNamespaceImport(TSNode node, Cursor& cursor, TypeHandle& handle, DeclarationFlags& flags)
{
	cursor.Child();
	cursor.Sibling();
	auto nameNode = cursor.Current();
	auto startOffset = ts_node_start_byte(nameNode) + 1;
	auto endOffset = ts_node_end_byte(nameNode) - 1;
	buffer_view view = buffer_view(startOffset, endOffset, buffer);
	auto moduleNameHash = StringHash(view);

	// copy and pasted from handle import node
	if (auto mod = g_modules.Read(moduleNameHash))
	{
		if (mod.value()->moduleFile->status == Status::dirty)
			loadFutures.push_back(std::async(std::launch::async, HandleLoad, mod.value()->moduleFileHash));
	}
	else
	{
		auto moduleName = view.Copy();
		if (auto modulePath = FindModuleFilePath(moduleName))
		{
			// then we need to create the module.
			auto mod = RegisterModule(moduleName, *modulePath);
			loadFutures.push_back(std::async(std::launch::async, HandleLoad, mod->moduleFileHash));
		}
	}

	if (auto modopt = g_modules.Read(moduleNameHash))
	{
		auto mod = *modopt;
		handle.fileIndex = mod->moduleFile->fileIndex;
		handle.index = 0; // 0?
		handle.scope = mod->moduleFile->file;
		flags = flags | DeclarationFlags::Evaluated;
	}

	cursor.Parent();
}


void FileScope::HandleNamedDecl(const TSNode nameNode, ScopeHandle currentScope, std::vector<TSNode>& structs, bool exporting, bool usingFlag)
{
	auto cursor = Cursor();
	std::vector<TSNode> identifiers;
	TSNode rhs;
#if _DEBUG
	rhs = { 0 };
#endif

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
	if (ts_node_has_error(cursor.Current()))
		return;

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
	if (usingFlag)
	{
		flags = flags | DeclarationFlags::Using;
	}

	TypeHandle handle = TypeHandle::Null();

	if (exporting)
		flags = flags | DeclarationFlags::Exported;

	if (expressionType == g_constants.constDecl || expressionType == g_constants.varDecl)
	{
		//@TODO doesn't handle compound declarations.
		if(expressionType == g_constants.constDecl)
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

		auto rhsType = ts_node_symbol(rhs);
		if (rhsType == g_constants.import) // this is probably the wrong place to do this, but whatever.
		{
			HandleNamespaceImport(rhs, cursor, handle, flags);
		}

	}
	else if (expressionType == g_constants.funcDefinition)
	{
		handle = HandleFuncDefinitionNode(node, currentScope, structs, flags);
	}
	else if (expressionType == g_constants.structDecl || expressionType == g_constants.unionDecl || expressionType == g_constants.enumDecl)
	{
		if( expressionType == g_constants.enumDecl )
			flags = flags | DeclarationFlags::Enum;
		else
			flags = flags | DeclarationFlags::Struct;
		
		flags = flags | DeclarationFlags::Constant;

		auto declarationNode = cursor.Current();
		structs.push_back(declarationNode);

		flags = flags | DeclarationFlags::Evaluated;
		// descend into struct decl to find the scope.
		cursor.Child();
		while (cursor.Sibling())
		{
			auto scopeNode = cursor.Current();
			auto symbol = ts_node_symbol(scopeNode);
			if (symbol == g_constants.dataScope)
			{

				if (auto scopeHandle = GetScopeFromOffset(declarationNode.context[0], edits, editCount))
				{
					handle = GetScope(*scopeHandle)->associatedType;
				}
				else
				{
					handle = AllocateType();
					auto king = &types[handle.index];
					king->name = GetIdentifierFromBufferCopy(identifiers[0], buffer); //@todo this is probably not ideal, allocating a string for every type name every time.
					handle.scope = AllocateScope(declarationNode, currentScope, false);
					GetScope(handle.scope)->associatedType = handle;
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
		// is it even p;ossible to get down here?
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
		HandleNamedDecl(child, scope, structs, exporting, true);
	}
	else
	{
		//@todo if this is a type definition, then we need to handle that eventually

		AddUsingToScope(child, buffer, GetScope(scope), TypeHandle::Null(), DeclarationFlags::Using);
	}



}

void FileScope::HandleForLoop(TSNode scopeNode, ScopeHandle scopeHandle, Cursor& cursor)
{
	static auto itHash = StringHash("it");
	static auto it_indexHash = StringHash("it_index");

	// so first find out if we have declared any iterators, and if not then add 
	cursor.Child(); // for token
	cursor.Sibling(); // range

	auto symbol = cursor.Symbol();
	if (symbol == g_constants.identifier) // then this is an iterator declaration
	{
		auto node = cursor.Current();
		auto scope = GetScope(scopeHandle);
		cursor.Sibling(); // : token or , token
		cursor.Sibling(); // range declaration or identifier

		auto secondDecl = cursor.Current();
		if (ts_node_symbol(secondDecl) == g_constants.identifier)
		{
			// this is the index declaration
			cursor.Sibling(); // : token
			cursor.Sibling(); // range

			cursor.Child(); // this is probably an identifier now
			auto rhsNode = cursor.Current();
			AddEntryToScope(node, buffer, scope, TypeHandle::Null(), DeclarationFlags::Iterator, rhsNode);
			AddEntryToScope(secondDecl, buffer, scope, intType, DeclarationFlags::Evaluated, { 0 });
		}
		else
		{
			cursor.Child(); // this is probably an identifier now
			auto rhsNode = cursor.Current();
			AddEntryToScope(node, buffer, scope, TypeHandle::Null(), DeclarationFlags::Iterator, rhsNode);

			ScopeDeclaration itIndexDecl;
			itIndexDecl.startByte = scopeNode.context[0];
			itIndexDecl.flags = DeclarationFlags::Evaluated;
			itIndexDecl.type = intType;
			itIndexDecl.SetLength(0);
			scope->Add(it_indexHash, itIndexDecl);
		}
		cursor.Parent();
	}
	else // probably a range declaration ??
	{
		cursor.Child(); // this is probably an identifier now
		// find the type of the identifier ??

		auto rhsNode = cursor.Current();

		// add two special entries to the sccope, it and it_index
		auto scope = GetScope(scopeHandle);

		// maybe these can be constant hashes?
		ScopeDeclaration itDecl;
		ScopeDeclaration itIndexDecl;

		itDecl.startByte = scopeNode.context[0];
		itDecl.SetLength(0);
		itDecl.flags = DeclarationFlags::Iterator;

		auto offset = rhsNode.context[0] - itDecl.startByte;
		itDecl.SetRHSOffset(offset);
		itDecl.id = rhsNode.id;

		itIndexDecl.startByte = scopeNode.context[0];
		itIndexDecl.flags = DeclarationFlags::Evaluated;
		itIndexDecl.type = intType;
		itIndexDecl.SetLength(0);

		scope->Add(itHash, itDecl);
		scope->Add(it_indexHash, itIndexDecl);

		cursor.Parent();
	}


	while (cursor.Sibling()); // imperative scope or statement !
}

void FileScope::HandleIfStatement(ScopeHandle scope, Cursor& cursor, std::vector<TSNode>& structs)
{
	/*
	if_statement: $ = > prec.left(seq(
	"if",
	$._expression,
	optional("then"),
	$._statement,
	optional($.else_statement),
	)),

	else_statement: $ = > seq(
		"else",
		$._statement
	),
	*/

	cursor.Child(); // if token
	cursor.Sibling(); // condition
	cursor.Sibling(); // statment or "then"
	auto node = cursor.Current();
	if (!ts_node_is_named(node))
	{
		// it was a "then" keyword.
		cursor.Sibling(); // statement 

	}

	AllocateScope(node, scope, true);
	structs.push_back(node);

	// then find the else statement if it exists
	if (cursor.Sibling()) // else statement
	{
		cursor.Child(); // else token
		cursor.Sibling(); // statement

		auto node = cursor.Current();
		AllocateScope(node, scope, true);
		structs.push_back(node);

		cursor.Parent();
	}


	cursor.Parent();
}


void FileScope::FindDeclarations(TSNode scopeNode, ScopeHandle scope,  bool& exporting, bool rebuild)
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
	bool enumDecl = false;
	auto scopeSymbol = ts_node_symbol(scopeNode);

	if (scopeSymbol == g_constants.funcDefinition)
	{
		HandleFunctionDefnitionParameters(scopeNode, scope, cursor);
	}
	else if (scopeSymbol == g_constants.enumDecl)
	{
		enumDecl = true;
		cursor.Child(); // inside enum
		while (cursor.Sibling()); // should be the data scope.
	}
	else if (scopeSymbol == g_constants.structDecl)
	{
		cursor.Child(); // inside struct
		while (cursor.Sibling()); // should be the data scope.
	}
	else if (scopeSymbol == g_constants.forLoop)
	{
		
		HandleForLoop(scopeNode, scope, cursor);

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
			AllocateScope(node, scope, true);
			structs.push_back(node);
		}
		else if (type == g_constants.forLoop)
		{
			AllocateScope(node, scope, true);
			structs.push_back(node);
		}
		else if (type == g_constants.scopeExport)
			exporting = true;

		else if (type == g_constants.scopeFile)
			exporting = false;
		else if (type == g_constants.identifier && enumDecl)
		{
			// if this is an identifier by itself, in an enum, then it is a declaration.
			auto scopePtr = GetScope(scope);
			AddEntryToScope(node, buffer, scopePtr, scopePtr->associatedType, DeclarationFlags::Evaluated | DeclarationFlags::Constant, { 0 });
		}
		else if (type == g_constants.ifStatement || type == g_constants.elseStatement || type == g_constants.whileLoop)
		{
			_nodeToScopes.insert(std::make_pair(node.id, scope));
			structs.push_back(node);
		}
		else if (type == g_constants.import)
		{
			HandleImportNode(node);
		}
		else if (type == g_constants.load)
		{
			HandleLoadNode(node);
		}


	} while (cursor.Sibling());



	// when rebuilding, we only want to find declarations for scopes that don't exist yet!
	// i think we can find out which scopes don't exist from checking the offsets
	for (auto structNode : structs)
	{
		auto scopeHandle = GetScopeFromNodeID(structNode.id);
		FindDeclarations(structNode, scopeHandle, exporting);
	}


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
		auto symbol = ts_node_symbol(parameterNode);
		if (symbol == g_constants.parameter)
		{
			king->parameters.push_back(GetIdentifierFromBufferCopy(parameterNode, buffer));

			//get identifier
			cursor.Child();

			DeclarationFlags flags = DeclarationFlags::None;
			auto childType = cursor.Symbol();
			if (childType == g_constants.usingExpression)
			{
				flags = flags | DeclarationFlags::Using;
				cursor.Child();
				cursor.Sibling();
			}

			auto identifierNode = cursor.Current();
			

			if (cursor.Sibling()) // always ':', probably. it may be possible to have a weird thing here where we dont have an rhs
			{
				cursor.Sibling(); // rhs expression, could be expression, variable initializer single, const initializer single
				auto rhsNode = cursor.Current();
				AddEntryToScope(identifierNode, buffer, GetScope(currentScope), TypeHandle::Null(), flags, rhsNode);
			}

			if ((flags & DeclarationFlags::Using) != 0)
			{
				cursor.Parent();
			}

			cursor.Parent();
		}
		else if (symbol == g_constants.returnTypes)
		{
			
			cursor.Child();

			while (cursor.Sibling()) // first call skips the "("
			{
				auto parameterNode = cursor.Current();
				auto symbol = ts_node_symbol(parameterNode);
				if (symbol == g_constants.parameter)
				{
					//get identifier
					cursor.Child();

					DeclarationFlags flags = DeclarationFlags::Return;

					auto identifierNode = cursor.Current();

					if (cursor.Sibling()) // always ':', probably. it may be possible to have a weird thing here where we dont have an rhs
					{
						cursor.Sibling(); // rhs expression, could be expression, variable initializer single, const initializer single
						auto rhsNode = cursor.Current();
						AddEntryToScope(identifierNode, buffer, GetScope(currentScope), TypeHandle::Null(), flags, rhsNode);
					}
					else
					{
						AddUsingToScope(identifierNode, buffer, GetScope(currentScope), TypeHandle::Null(), flags);
					}

					cursor.Parent();
				}
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

	flags = flags | DeclarationFlags::Function | DeclarationFlags::Evaluated | DeclarationFlags::Constant;

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
	auto scopeHandle = AllocateScope(node, currentScope, true);
	GetScope(scopeHandle)->associatedType = typeHandle;
	typeHandle.scope = scopeHandle;
	structs.push_back(node); // not the scope node! this is how we can know we're doing a function definition later.

	return typeHandle;
}



void FileScope::HandleImportNode(TSNode node)
{
	// import apparently just imports the exported list from a module / file (?) into the current scope.
	// this can maybe just be done like 'using' but i don't know if there is something more subtle going on with regard to overload resolution or not.

	auto nameNode = ts_node_named_child(node, 0);
	auto startOffset = ts_node_start_byte(nameNode) + 1;
	auto endOffset = ts_node_end_byte(nameNode) - 1;
	buffer_view view = buffer_view(startOffset, endOffset, buffer);
	auto moduleNameHash = StringHash(view);
	imports.push_back(moduleNameHash);

	if (auto mod = g_modules.Read(moduleNameHash))
	{
		if (mod.value()->moduleFile->status == Status::dirty)
			loadFutures.push_back(std::async(std::launch::async, HandleLoad, mod.value()->moduleFileHash));
	}
	else
	{
		auto moduleName = view.Copy();
		if (auto modulePath = FindModuleFilePath(moduleName))
		{
			// then we need to create the module.
			auto mod = RegisterModule(moduleName, *modulePath);
			loadFutures.push_back(std::async(std::launch::async, HandleLoad, mod->moduleFileHash));
		}
	}
}

void FileScope::HandleLoadNode(TSNode node)
{
	// a load actually puts a file into the module's scope. It doesn't matter which file the load occurs in.

	auto nameNode = ts_node_named_child(node, 0);
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

		if (auto fileOpt = g_fileScopes.Read(loadNameHash))
		{
			auto file = fileOpt.value();
			if (file->status == Status::dirty)
				loadFutures.push_back(std::async(std::launch::async, HandleLoad, loadNameHash));
		}
		else
		{
			loadFutures.push_back(std::async(std::launch::async, HandleLoad, loadNameHash));
		}

	}

	// else raise diagnostic that this load was not found
}

void FileScope::CreateTopLevelScope(TSNode node, ScopeStack& stack, bool& exporting)
{
	file = AllocateScope(node, { UINT16_MAX }, false);
	auto self = AllocateType();
	auto king = &types[self.index];
	king->name = "namespace";

	loads.push_back(StringHash("builtin"));
	//if(documentHash != preloadHash)
		//loads.push_back(preloadHash);


	FindDeclarations(node, file, exporting);
}


void FileScope::CheckScope(Scope* scope)
{
	auto size = scope->declarations.Size();

	for (int i = 0; i < size; i++)
	{
		auto decl = scope->GetDeclFromIndex(i);

		if (!decl->HasFlags(DeclarationFlags::Evaluated))
		{
			auto node = ConstructRhsFromDecl(*decl, currentTree);

			if (auto typeHandle = EvaluateNodeExpressionType(node, scope))
			{
				decl->type = *typeHandle;
				decl->flags = decl->flags | DeclarationFlags::Evaluated;
			}
		}

		// if this has usings, we need to inject them.
		if (decl->HasFlags(DeclarationFlags::Evaluated | DeclarationFlags::Using))
		{
			// add members of type to scope.
			auto typeHandle = decl->type;
			auto memberFile = g_fileScopeByIndex.Read(typeHandle.fileIndex);
			auto memberScope = memberFile->GetScope(typeHandle.scope);

			// we probably need to make sure we don't have any circular dependencies in here.
			if (!memberScope->checked)
				memberFile->CheckScope(memberScope);

			memberScope->InjectMembersTo(scope, decl->startByte);
			size += memberScope->declarations.Size();

			decl = scope->GetDeclFromIndex(i);
			decl->flags = (DeclarationFlags)(decl->flags & (~DeclarationFlags::Using));
			if (decl->HasFlags(DeclarationFlags::Expression))
			{
				decl->SetLength(0); // this is a hack so that this 'using' declaration doesn't show up in completions.
				//decl->flags = (DeclarationFlags)(decl->flags & (~DeclarationFlags::Expression));
			}
		}

			
		
		if (decl->HasFlags(DeclarationFlags::Return | DeclarationFlags::Evaluated))
		{
			auto king = GetType(scope->associatedType);
			king->returnTypes.push_back(decl->type);

			if (decl->HasFlags(DeclarationFlags::Expression))
			{
				decl->SetLength(0);
			}
		}
		
		if (decl->HasFlags(DeclarationFlags::Iterator | DeclarationFlags::Evaluated)) 
		{
			// if this is an iterator, then we want to dereference the type.
			bool valid = decl->type.Dereference();
			//assert(valid);
		}

	}

	scope->checked = true;
}


void FileScope::DoTypeCheckingAndInference(TSTree* tree)
{
	// so i think that we should be able to do this in order of scopes.
	for (auto& scope : scopeKings)
	{
		if(!scope.checked)
			CheckScope(&scope);
	}
}

void FileScope::WaitForDependencies()
{
	// this will deadlock if modules import themselves, which i *think* is allowed.
	// an alternative is to wait for the individual condition variables of each load
	// but that presents a possibility where we get to this point and the condition variables havent been created yet!
	for (auto& future : loadFutures)
	{
		future.wait();
	}
}


void FileScope::Build()
{

	auto dirtyStatus = Status::dirty;
	if (!status.compare_exchange_strong(dirtyStatus, Status::buliding))
	{
		return;
	}

	Clear();

	buffer = g_buffers.Read(documentHash).value();
	auto tree = g_trees.Read(documentHash).value();
	auto root = ts_tree_root_node(tree);

	currentTree = tree;

	ScopeStack stack;

	bool exporting = true;
	bool skipNextImperative = false;

	CreateTopLevelScope(root, stack, exporting);

	status = Status::scopesBuilt;
}



const std::optional<TypeHandle> FileScope::GetTypeFromSymbol(TSNode node, Scope* scope, TSSymbol symbol)
{
	if (symbol == g_constants.integerLiteral)
	{
		return intType;
	}
	else if (symbol == g_constants.stringLiteral)
	{
		return stringType;
	}
	else if (symbol == g_constants.floatLiteral)
	{
		return floatType;
	}
	else if (symbol == g_constants.unaryExpression)
	{

		auto unaryOp = ts_node_child(node, 0);
		auto expr = ts_node_child(node, 1);
		auto unarySymbol = ts_node_symbol(unaryOp);
		// auto exprSymbol = ts_node_symbol(expr);

		if (unarySymbol == g_constants.pointerTo)
		{
			if (auto type = EvaluateNodeExpressionType(expr, scope))
			{
				auto enoughRoom = type->Push(TypeAttribute::pointerTo);
				assert(enoughRoom);

				return type;
			}
		}
		else if (unarySymbol == g_constants.arrayDecl)
		{
			if (auto type = EvaluateNodeExpressionType(expr, scope))
			{
				auto enoughRoom = type->Push(TypeAttribute::arrayOf);
				assert(enoughRoom);

				return type;
			}
		}
	}
	else if (symbol == g_constants.functionCall)
	{
		auto functionName = ts_node_named_child(node, 0);
		FileScope* declFile;
		Scope* declScope;

		auto declIndex = GetDeclarationForNodeFromScope(functionName, this, scope, &declFile, &declScope);
		if (declIndex >= 0)
		{

			// in case you are tempted to uncomment this code, know that it causes infinite loops.
			/*
			if (!declScope->checked)
			{
				declFile->CheckScope(declScope);
			}
			*/

			auto decl = declScope->GetDeclFromIndex(declIndex);
			if (!decl->HasFlags(DeclarationFlags::Evaluated))
			{
				auto node = ConstructRhsFromDecl(*decl, currentTree);

				if (auto typeHandle = EvaluateNodeExpressionType(node, scope))
				{
					decl->type = *typeHandle;
					decl->flags = decl->flags | DeclarationFlags::Evaluated;
				}
				else
				{
					return std::nullopt;
				}
			}

			// and check the scope of the function's type, to infer returns
			auto funcScope = declFile->GetScope(decl->type.scope);
			if (!funcScope->checked)
				declFile->CheckScope(funcScope);

			auto king = GetType(decl->type);
			if(king->returnTypes.size() > 0)
				return king->returnTypes[0];
		}
	}
	else if (symbol == g_constants.memberAccess)
	{
		return EvaluateMemberAccessType(node, this, scope);
	}

	return std::nullopt;
}

const std::optional<TypeHandle> FileScope::EvaluateNodeExpressionType(TSNode node, Scope* startScope)
{
	auto symbol = ts_node_symbol(node);
	auto hash = GetIdentifierHash(node, buffer);
	auto scope = startScope;

	if (symbol == g_constants.identifier)
	{
		// identifier of some kind. struct, union, or enum, or also a built in type now. we don't treat those differently than identifiers.

		while (scope)
		{
			//assert(scope == startScope || scope->checked);

			auto declIndex = scope->GetIndex(hash);
			if (declIndex >= 0)
			{
				auto decl = scope->GetDeclFromIndex(declIndex);
				auto startByte = ts_node_start_byte(node);

				auto notInImperativeOrder = !decl->HasFlags(DeclarationFlags::Constant) && scope->imperative && (decl->startByte > startByte);
				auto expressionType = decl->HasFlags(DeclarationFlags::Expression); // this will be declaring the name of the thing in the same scope we're looking for the RHS, and that will cause an infinite loop.
				if (notInImperativeOrder || expressionType)
				{
					scope = GetScope(scope->parent);
					continue;
				}

				if (decl->flags & DeclarationFlags::Evaluated)
				{
					return decl->type;
				}
				else
				{
					auto rhsNode = ConstructRhsFromDecl(*decl, currentTree);
					if (rhsNode.id == node.id)
						return std::nullopt;

					if (auto type = EvaluateNodeExpressionType(rhsNode, scope))
					{
						auto memberScope = GetScope(type->scope);
						if (memberScope != startScope && !memberScope->checked)
							CheckScope(memberScope);

						decl->type = *type;
						decl->flags = decl->flags | DeclarationFlags::Evaluated;

						return type;
					}

					return std::nullopt;
				}
			}

			scope = GetScope(scope->parent);
		}
		
		FileScope* moduleFile;
		Scope* declScope;
		auto declIndex = SearchAndGetModule(hash, &moduleFile, &declScope);
		if (declIndex >= 0)
		{
			/*
			if (declScope != startScope && !declScope->checked)
			{
				moduleFile->CheckScope(declScope); // this may be unnecesary 
			}
			*/

			auto decl = declScope->GetDeclFromIndex(declIndex);
			if (decl->flags & DeclarationFlags::Evaluated)
			{
				auto memberScope = moduleFile->GetScope(decl->type.scope);
				if (memberScope != startScope && !memberScope->checked)
					moduleFile->CheckScope(memberScope);

				return decl->type;
			}
			else
			{
				auto node = ConstructRhsFromDecl(*decl, moduleFile->currentTree);
				if (auto type = moduleFile->EvaluateNodeExpressionType(node, declScope))
				{
					decl->type = *type;
					decl->flags = decl->flags | DeclarationFlags::Evaluated;
					auto memberScope = moduleFile->GetScope(type->scope);
					if (memberScope != startScope && !memberScope->checked)
						moduleFile->CheckScope(memberScope);

					return type;
				}


				return std::nullopt;
			}
		}
	}
	else
	{
		return GetTypeFromSymbol(node, scope, symbol);
	}

	return std::nullopt;
}



void FileScope::RebuildScope(TSNode newScopeNode, TSInputEdit* edits, int editCount, TSNode root)
{
	/*
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
		FindDeclarations(newScopeNode, handle, exporting, true);
		
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
	*/
}


