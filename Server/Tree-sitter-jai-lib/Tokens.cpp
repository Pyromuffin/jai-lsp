#include "FileScope.h"


void FileScope::HandleMemberReference(TSNode rhsNode, ScopeHandle scope)
{
	auto rhsHash = GetIdentifierHash(rhsNode, buffer);

	auto start = ts_node_start_point(rhsNode);
	auto end = ts_node_end_point(rhsNode);
	SemanticToken token;
	token.col = start.column;
	token.line = start.row;
	token.length = end.column - start.column;
	token.modifier = (TokenModifier)0;

	FileScope* declFile;
	Scope* declScope;
	auto declIndex = GetDeclarationForNode(rhsNode, this, GetScope(scope), &declFile, &declScope); // this should probably use the scope stack for better performance
	if (declIndex >= 0)
	{
		token.type = GetTokenTypeFromFlags(declScope->GetDeclFromIndex(declIndex)->flags);
		tokens.push_back(token);
		return;
	}

	// if we get here then we've got an unresolved identifier, that we will check when we've parsed the entire document.
	token.type = (LSP_TokenType)-1;

	tokens.push_back(token);
	return;

}




static std::optional<SemanticToken> HandleVariableReferenceFromScope(TSNode node, Scope* scope, FileScope* file)
{
	auto hash = GetIdentifierHash(node, file->buffer);

	auto start = ts_node_start_point(node);
	auto end = ts_node_end_point(node);
	SemanticToken token;
	token.col = start.column;
	token.line = start.row;
	token.length = end.column - start.column;
	token.modifier = (TokenModifier)0;


	while (scope)
	{
		if (auto decl = scope->TryGet(hash))
		{

			// also we need to taken into account imperative scope order.
			bool notImperative = !scope->imperative || decl->HasFlags(DeclarationFlags::Constant);
			bool imperativeOrder = scope->imperative && decl->startByte <= ts_node_start_byte(node);
			bool expressionType = decl->HasFlags(DeclarationFlags::Expression);
			if ((notImperative || imperativeOrder) && !expressionType)
			{
				token.type = GetTokenTypeFromFlags(decl->flags);
				goto done;
			}
		}

		scope = file->GetScope(scope->parent);
	}

	if (FileScope::builtInScope->TryGet(hash))
	{
		return std::nullopt;
	}


	// search modules
	if (auto decl = file->SearchModules(hash))
	{
		token.type = GetTokenTypeFromFlags(decl->flags);
		goto done;
	}

	token.type = (LSP_TokenType)-1;

done:
	return token;
}


void FileScope::HandleVariableReference(TSNode node, ScopeHandle scopeHandle)
{
	Scope* scope = GetScope(scopeHandle);
	if (auto token = HandleVariableReferenceFromScope(node, scope, this))
		tokens.push_back(*token);
}


void FileScope::DoTokens2()
{
	/*
	auto thread = ::GetCurrentThread();
	auto pathStatus ="tokens: " + documentHash.debug_name;
	auto pathLength = strlen(pathStatus.c_str()) + 1;
	wchar_t* wide = new wchar_t[pathLength];

	size_t converted;
	mbstowcs_s(&converted, wide, pathLength, pathStatus.c_str(), _TRUNCATE);
	SetThreadDescription(thread, wide);

	delete[] wide;
	*/

	if (status == Status::scopesBuilt)
	{
		WaitForDependencies();
		DoTypeCheckingAndInference(currentTree);

		status = Status::checked;
	}

	auto root = ts_tree_root_node(currentTree);
	ScopeStack stack;
	stack.scopes.push_back(file);
	std::vector<TSNode> unresolvedEntry;
	std::vector<int> unresolvedTokenIndex;

	bool exporting = true;
	bool skipNextImperative = false;

	static const auto queryText =
		"(function_definition) @func_defn" // hopefully this matches before the identifiers does.
		"(for_loop) @func_defn"			   // not really a function definition but it shares the same structure
		"(imperative_scope) @imperative.scope"
		"(struct_definition) @data.scope"
		"(enum_definition) @data.scope"
		"(member_access . (_) (_) @member_rhs )"
		"(identifier) @var_ref"
		"(block_end) @block_end"
		"(export_scope_directive) @export"
		"(file_scope_directive) @file"
		"(argument_name) @argument"
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
		structDecl,
		member_rhs,
		var_ref,
		block_end,
		export_scope,
		file_scope,
		argument,
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
		case ScopeMarker::structDecl:
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
			}

			auto scopeHandle = GetScopeFromNodeID(node.id);
			stack.scopes.push_back(scopeHandle);

			offsetToHandle.Add(ts_node_start_byte(node), scopeHandle);
			break;
		}
		case ScopeMarker::member_rhs:
		{
			HandleMemberReference(node, stack.scopes.back());
			identifiersToSkip++;
			break;
		}
		case ScopeMarker::argument:
		{
			// we need to go up two nodes to get the function call
			auto identifier = ts_node_named_child(node, 0);
			auto func = ts_node_parent(ts_node_parent(node));
			auto functionName = ts_node_named_child(func, 0);
			FileScope* declFile;
			Scope* declScope;

			auto declIndex = GetDeclarationForNodeFromScope(functionName, this, GetScope(stack.scopes.back()), &declFile, &declScope);
			if (declIndex >= 0)
			{
				auto decl = declScope->GetDeclFromIndex(declIndex);
				auto members = declFile->GetScope(decl->type.scope);
				if (auto token = HandleVariableReferenceFromScope(identifier, members, declFile))
					tokens.push_back(*token);
			}
			else
			{
				HandleVariableReference(identifier, stack.scopes.back());
			}

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

			HandleVariableReference(node, stack.scopes.back());
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


/*
void FileScope::DoTokens(TSNode root, TSInputEdit* edits, int editCount)
{
	const static auto queryText =
		"(function_definition) @func_defn" // hopefully this matches before the identifiers does.
		"(for_loop) @func_defn"			   // this is not actually a func definition but it does have the same structure.
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
				scopeHandle = AllocateScope(node, stack.scopes.back(), true);
				FindDeclarations(node, *scopeHandle, exporting);
			}

			stack.scopes.push_back(*scopeHandle);
			newOffsets.Add(node.context[0], *scopeHandle);
			SetScopePresentBit(*scopeHandle);
			break;
		}
		case ScopeMarker::member_rhs:
		{
			HandleMemberReference(node, stack.scopes.back());
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

			HandleVariableReference(node, stack.scopes.back());
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
*/