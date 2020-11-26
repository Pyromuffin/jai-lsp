#include "TreeSitterJai.h"
#include "FileScope.h"

static bool IsMemberAccess(TSSymbol symbol)
{
	return symbol == g_constants.memberAccess || symbol == g_constants.memberAccessNothing;

}



const TypeKing* GetType(TypeHandle handle)
{
	if (handle == TypeHandle::Null())
		return nullptr;

	return &g_fileScopeByIndex.Read(handle.fileIndex)->types[handle.index];
}

std::optional<ScopeDeclaration> GetDeclarationForNodeFromScope(TSNode node, FileScope* fileScope, const GapBuffer* buffer, Scope* scope, TSNode parent)
{
	// search scopes going up for entries, if they're data scopes. if they're imperative scopes then declarations have to be in order.
	auto identifierHash = GetIdentifierHash(node, buffer);
	while (scope != nullptr)
	{
		if (auto decl = scope->TryGet(identifierHash))
		{
			// if the scope is imperative, then respect ordering, if it is not, just get the decl.
			if (scope->imperative)
			{
				auto definitionStart = decl.value().startByte;
				auto identifierStart = ts_node_start_byte(node);
				if (identifierStart >= definitionStart)
				{
					return decl;
				}
			}
			else
			{
				return decl;
			}
		}

		scope = fileScope->GetScope(scope->parent);
	}
	

	return fileScope->Search(identifierHash);
}


std::optional<ScopeDeclaration> GetDeclarationForRootMemberNode(TSNode node, FileScope* fileScope, const GapBuffer* buffer, Scope** outScope, FileScope** outFile)
{
	TSNode parent;
	ScopeHandle handle;
	auto found = GetScopeAndParentForNode(node, fileScope, &parent, &handle);

	if (!found)
		return std::nullopt;

	*outScope = fileScope->GetScope(handle);
	return GetDeclarationForNodeFromScope(node, fileScope, buffer, fileScope->GetScope(handle), parent);
}


TSNode ConstructRhsFromDecl(ScopeDeclaration decl, TSTree* tree);

static inline std::optional<ScopeDeclaration> Evaluate(ScopeDeclaration* decl)
{

}

std::optional<ScopeDeclaration> EvaluateMemberAccess(TSNode node, FileScope* fileScope, const GapBuffer* buffer, Scope** outScope, FileScope** outFile)
{
	// rhs should always be an identifier ?

	auto lhs = ts_node_named_child(node, 0);
	auto lhsSymbol = ts_node_symbol(lhs);
	std::optional<ScopeDeclaration> lhsType = std::nullopt;

	if (IsMemberAccess(lhsSymbol))
	{
		lhsType = EvaluateMemberAccess(lhs, fileScope, buffer, outScope, outFile);
	}
	else
	{
		if (auto decl = GetDeclarationForRootMemberNode(lhs, fileScope, buffer, outScope, outFile) )
		{
			lhsType = decl;
		}
	}

	if (!lhsType)
		return std::nullopt;

	if ( !lhsType->HasFlags(DeclarationFlags::Evaluated) )
	{
		// so we need to get the file scope for this declaration as well

		auto rhsNode = ConstructRhsFromDecl(*lhsType, (*outFile)->currentTree);
		if (auto typeHandle = (*outFile)->EvaluateNodeExpressionType(rhsNode, *outScope))
		{
			lhsType->type = *typeHandle;
			lhsType->flags = lhsType->flags | DeclarationFlags::Evaluated;
		}
		else
		{
			return std::nullopt;
		}
	}

	if (ts_node_symbol(node) == g_constants.memberAccessNothing)
		return lhsType;

	auto rhs = ts_node_named_child(node, 1);
	auto rhsHash = GetIdentifierHash(rhs, buffer);
	auto typeKing = GetType(lhsType->type);

	// search for rhs in the members of the LHS type

	if (typeKing)
	{
		auto file = g_fileScopeByIndex.Read(lhsType->type.fileIndex);
		auto members = &file->scopeKings[typeKing->members.index];
		if (!members->checked)
		{
			file->CheckScope(members);
		}

		if (auto rhsDecl = members->TryGet(rhsHash))
		{
			*outFile = file;
			return rhsDecl;
		}
	}

	return std::nullopt;
}


static std::optional<TypeHandle> EvaluateMemberAccessType(TSNode node, FileScope* fileScope, Scope* scope)
{
	auto lhs = ts_node_named_child(node, 0);
	auto lhsSymbol = ts_node_symbol(lhs);
	std::optional<TypeHandle> lhsType = std::nullopt;

	if (IsMemberAccess(lhsSymbol))
	{
		lhsType = EvaluateMemberAccessType(lhs, fileScope, scope);
	}
	else
	{
		// root of member access
		if (auto type = fileScope->EvaluateNodeExpressionType(lhs, scope))
		{
			lhsType = type;
		}
	}

	if (!lhsType)
		return std::nullopt;

	if (ts_node_symbol(node) == g_constants.memberAccessNothing)
		return lhsType;

	auto rhs = ts_node_named_child(node, 1);
	auto rhsHash = GetIdentifierHash(rhs, fileScope->buffer);
	// search for rhs in the members of the LHS type

	{
		auto file = g_fileScopeByIndex.Read(lhsType->fileIndex);
		auto king = &file->types[lhsType->index];
		auto members = &file->scopeKings[king->members.index];
		if (!members->checked)
		{
			file->CheckScope(members);
		}

		if (auto rhsDecl = members->TryGet(rhsHash))
		{
			if(rhsDecl->HasFlags(DeclarationFlags::Evaluated))
				return rhsDecl->type;
		}
	}

	return std::nullopt;
}


std::optional<ScopeDeclaration> GetDeclarationForNode(TSNode node, FileScope* fileScope, const GapBuffer* buffer)
{
	/*
				---------------
			   |			  |
		--------------        |
		|            |		  |
	---------        |		  |
	|       |        |		  |
	a   .   b    .   c    .   d

	*/

	// if this is an identifier, then go up until we find either a scope or a member access.
	// if this is a member access, then get the type of the RHS, which will require getting the type of the LHS.

	auto nodeSymbol = ts_node_symbol(node);
	if (IsMemberAccess(nodeSymbol))
	{
		Scope* s;
		FileScope* file = fileScope;
		return EvaluateMemberAccess(node, fileScope, buffer, &s, &file);
	}

	// identifier or something worse!

	auto parent = ts_node_parent(node);
	bool inFileScope = false;
	bool terminalLHS = false;

	while (!fileScope->ContainsScope(parent.id))
	{
		if (ts_node_is_null(parent))
		{
			inFileScope = true;
			break;
		}

		if (!terminalLHS)
		{
			auto parentSymbol = ts_node_symbol(parent);
			if (IsMemberAccess(parentSymbol))
			{
				auto lhs = ts_node_named_child(parent, 0);
				if (lhs.id == node.id)
				{
					// root of the member access, so just zoop up the tree.
					terminalLHS = true;
				}
				else
				{
					Scope* s;
					FileScope* file = fileScope;
					return EvaluateMemberAccess(parent, fileScope, buffer, &s, &file);
				}
			}
		}

		parent = ts_node_parent(parent);
	}

	Scope* scope = nullptr;
	if (!inFileScope)
	{
		auto handle = fileScope->GetScopeFromNodeID(parent.id);
		scope = fileScope->GetScope(handle);
	}

	auto decl = GetDeclarationForNodeFromScope(node, fileScope, buffer, scope, parent);
	if (decl)
	{
		if (!decl->HasFlags(DeclarationFlags::Evaluated))
		{
			auto rhsNode = ConstructRhsFromDecl(*decl, fileScope->currentTree);
			if (auto typeHandle = fileScope->EvaluateNodeExpressionType(rhsNode, scope))
			{
				decl->type = *typeHandle;
				decl->flags = decl->flags | DeclarationFlags::Evaluated;
				return decl;
			}
		}
		else
		{
			return decl;
		}
	}

	return std::nullopt;
}


const std::optional<TypeHandle> GetTypeForNode(TSNode node, FileScope* file)
{
	// first is to get the scope for the node.
	if (auto scopehandle = GetScopeForNode(node, file))
	{
		auto scope = &file->scopeKings[scopehandle->index];

		auto nodeSymbol = ts_node_symbol(node);
		if (IsMemberAccess(nodeSymbol))
		{
			return EvaluateMemberAccessType(node, file, scope);
		}

		// identifier
		return file->EvaluateNodeExpressionType(node, scope);
	}


	return std::nullopt;
}


export_jai_lsp const char* GetLine(uint64_t hashValue, int row)
{
	thread_local std::string s;
	s.clear();

	auto documentName = Hash{ .value = hashValue };
	auto buffer = g_buffers.Read(documentName).value();
	buffer->GetRowCopy(row, s);

	return s.c_str();
}




export_jai_lsp void GetSignature(uint64_t hashValue, int row, int col, const char*** outSignature, int* outParameterCount, int* outActiveParameter, int* errorCount, Range** outErrors)
{
	thread_local std::vector<const char*> strings;
	strings.clear();

	thread_local std::vector<Range> errors;
	errors.clear();

	auto documentName = Hash{ .value = hashValue };

	auto tree = ts_tree_copy(g_trees.Read(documentName).value());
	auto root = ts_tree_root_node(tree);
	auto buffer = g_buffers.Read(documentName).value();
	auto fileScope = g_fileScopes.Read(documentName).value();

	auto point = TSPoint{ static_cast<uint32_t>(row), static_cast<uint32_t>(col) };
	auto node = ts_node_named_descendant_for_point_range(root, point, point);
	if (ts_node_has_error(node))
	{
		node = ts_node_child(node, 0);
	}

	// go up looking for a function call
	while (ts_node_symbol(node) != g_constants.functionCall)
	{
		node = ts_node_parent(node);
		if (ts_node_is_null(node))
		{
			*outSignature = nullptr;
			ts_tree_delete(tree);
			return;
		}
	}

	int childCount = ts_node_child_count(node);
	node = ts_node_child(node, 0); // get function name

	if (auto type = GetTypeForNode(node, fileScope))
	{
		auto king = GetType(*type);
		strings.push_back(king->name.c_str());

		*outParameterCount = (int)king->parameters.size();
		for (auto& str : king->parameters)
		{
			strings.push_back(str.c_str());
		}

		*outSignature = strings.data();
		*outActiveParameter = 0;

		// iterate through arguments to find which parameter index we are.
		int param = 0;
		node = ts_node_next_sibling(node); // go to "("
		auto startPos = ts_node_start_point(node);
		bool foundIt = false;
		int parametersProvided = 0;

		for (int i = 0; i < childCount - 3; i++)
		{
			node = ts_node_next_sibling(node); // go to first parameter or comma
			if (!ts_node_is_named(node))
			{
				// found a comma
				auto commaPos = ts_node_start_point(node);
				if (col <= (int)commaPos.column && row <= (int)commaPos.row)
				{
					foundIt = true;
				}

				if(!foundIt)
					param++;
			}
			else
			{
				// argument!
				parametersProvided++;

				if (parametersProvided > king->parameters.size())
				{
					auto errorStart = ts_node_start_point(node);
					auto errorEnd = ts_node_start_point(node);

					Range range;
					range.startCol = errorStart.column;
					range.startRow = errorStart.row;
					range.endCol = errorEnd.column;
					range.endRow = errorEnd.row;

					errors.push_back(range);
				}
			}
		}

		*errorCount = (int)errors.size();
		*outErrors = errors.data();
		*outActiveParameter = param;
	}


	ts_tree_delete(tree);
}


export_jai_lsp const char* Hover(uint64_t hashValue, int row, int col)
{
	auto documentName = Hash{ .value = hashValue };

	auto tree = ts_tree_copy(g_trees.Read(documentName).value());
	auto root = ts_tree_root_node(tree);
	auto buffer = g_buffers.Read(documentName).value();
	auto fileScope = g_fileScopes.Read(documentName).value();

	auto point = TSPoint{ static_cast<uint32_t>(row), static_cast<uint32_t>(col) };
	auto node = ts_node_named_descendant_for_point_range(root, point, point);
	if (ts_node_has_error(node))
	{
		node = ts_node_child(node, 0);
	}


	if (auto type = GetTypeForNode(node, fileScope))
	{
		auto king = GetType(*type);
		ts_tree_delete(tree);
		return king->name.c_str();
	}


	ts_tree_delete(tree);
	return nullptr;
}
