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


	return &g_fileScopeByIndex[handle.fileIndex]->types[handle.index];
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

		ScopeHandle handle;
		auto found = GetScopeAndParentForNode(parent, fileScope, &parent, &handle); // aliasing???
		if (found)
		{
			scope = fileScope->GetScope(handle);
		}
		else
		{
			scope = nullptr;
		}
	}

	return fileScope->Search(identifierHash);
}


std::optional<ScopeDeclaration> GetDeclarationForTopLevelNode(TSNode node, FileScope* fileScope, const GapBuffer* buffer)
{
	TSNode parent;
	ScopeHandle handle;
	auto found = GetScopeAndParentForNode(node, fileScope, &parent, &handle);

	if (!found)
		return std::nullopt;

	return GetDeclarationForNodeFromScope(node, fileScope, buffer, fileScope->GetScope(handle), parent);
}



std::optional<ScopeDeclaration> EvaluateMemberAccess(TSNode node, FileScope* fileScope, const GapBuffer* buffer)
{
	// rhs should always be an identifier ?
	
	auto lhs = ts_node_named_child(node, 0);
	auto lhsSymbol = ts_node_symbol(lhs);
	std::optional<ScopeDeclaration> lhsType = std::nullopt;

	if (IsMemberAccess(lhsSymbol))
	{
		lhsType = EvaluateMemberAccess(lhs, fileScope, buffer);
	}
	else
	{
		if (auto decl = GetDeclarationForTopLevelNode(lhs, fileScope, buffer))
		{
			
			lhsType = decl;
		}
	}

	if (!lhsType)
		return std::nullopt;

	if (ts_node_symbol(node) == g_constants.memberAccessNothing)
		return lhsType;

	auto rhs = ts_node_named_child(node, 1);
	auto rhsHash = GetIdentifierHash(rhs, buffer);
	auto typeKing = GetType(lhsType->type);

	if (typeKing)
	{
		auto members = &g_fileScopeByIndex[lhsType->type.fileIndex]->scopeKings[typeKing->members.index];

		if (auto rhsDecl = members->TryGet(rhsHash))
		{
			return rhsDecl;
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
		return EvaluateMemberAccess(node, fileScope, buffer);
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
					return EvaluateMemberAccess(parent, fileScope, buffer);
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
		return decl;
	}

	return std::nullopt;
}


const TypeKing* GetTypeForNode(TSNode node, FileScope* fileScope, GapBuffer* buffer)
{
	if (auto decl = GetDeclarationForNode(node, fileScope, buffer))
	{
		if (auto type = GetType(decl->type))
		{
			return type;
		}
	}

	return nullptr;
}


export const char* GetLine(uint64_t hashValue, int row)
{
	thread_local std::string s;
	s.clear();

	auto documentName = Hash{ .value = hashValue };
	auto buffer = g_buffers.Read(documentName).value();
	buffer->GetRowCopy(row, s);

	return s.c_str();
}




export void GetSignature(uint64_t hashValue, int row, int col, const char*** outSignature, int* outParameterCount, int* outActiveParameter, int* errorCount, Range** outErrors)
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

	if (auto type = GetTypeForNode(node, fileScope, buffer))
	{
		strings.push_back(type->name.c_str());

		*outParameterCount = type->parameters.size();
		for (auto& str : type->parameters)
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
				if (col <= commaPos.column && row <= commaPos.row)
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

				if (parametersProvided > type->parameters.size())
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

		*errorCount = errors.size();
		*outErrors = errors.data();
		*outActiveParameter = param;
	}


	ts_tree_delete(tree);
}


export const char* Hover(uint64_t hashValue, int row, int col)
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


	if (auto type = GetTypeForNode(node, fileScope, buffer))
	{
		ts_tree_delete(tree);
		return type->name.c_str();
	}


	ts_tree_delete(tree);
	return nullptr;
}




std::optional<ScopeDeclaration> EvaluateMemberAccessWithStack(TSNode node, FileScope* fileScope, const ScopeStack& stack, const GapBuffer* buffer)
{
	// rhs should always be an identifier ?

	auto lhs = ts_node_named_child(node, 0);
	auto lhsSymbol = ts_node_symbol(lhs);
	std::optional<ScopeDeclaration> lhsType = std::nullopt;

	if (IsMemberAccess(lhsSymbol))
	{
		lhsType = EvaluateMemberAccessWithStack(lhs, fileScope, stack, buffer);
	}
	else
	{
		if (auto decl = GetDeclarationForTopLevelNode(lhs, fileScope, buffer))
		{

			lhsType = decl;
		}
	}

	if (!lhsType)
		return std::nullopt;

	if (ts_node_symbol(node) == g_constants.memberAccessNothing)
		return lhsType;

	auto rhs = ts_node_named_child(node, 1);
	auto rhsHash = GetIdentifierHash(rhs, buffer);
	auto typeKing = GetType(lhsType->type);

	if (typeKing)
	{
		auto members = &g_fileScopeByIndex[lhsType->type.fileIndex]->scopeKings[typeKing->members.index];

		if (auto rhsDecl = members->TryGet(rhsHash))
		{
			return rhsDecl;
		}
	}

	return std::nullopt;
}




std::optional<ScopeDeclaration> GetDeclarationForNodeWithStack(TSNode node, FileScope* fileScope, const ScopeStack& stack, const GapBuffer* buffer)
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
		return EvaluateMemberAccessWithStack(node, fileScope, stack, buffer);
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
					return EvaluateMemberAccess(parent, fileScope, buffer);
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
		return decl;
	}

	return std::nullopt;
}
