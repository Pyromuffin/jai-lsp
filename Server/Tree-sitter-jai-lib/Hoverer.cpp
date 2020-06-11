#include "TreeSitterJai.h"

std::optional<ScopeDeclaration> GetDeclarationForNodeFromScope(TSNode node, FileScope* fileScope, GapBuffer* buffer, Scope* scope, TSNode parent)
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

		scope = GetScopeAndParentForNode(parent, fileScope, &parent); // aliasing???
	}

	return fileScope->Search(identifierHash);
}


std::optional<ScopeDeclaration> GetDeclarationForNode(TSNode node, FileScope* fileScope, GapBuffer* buffer)
{
	TSNode parent;
	auto scope = GetScopeAndParentForNode(node, fileScope, &parent);
	return GetDeclarationForNodeFromScope(node, fileScope, buffer, scope, parent);
}


Type* EvaluateMemberAccess(TSNode node, FileScope* fileScope, GapBuffer* buffer)
{
	// rhs should always be an identifier ?
	
	auto lhs = ts_node_named_child(node, 0);
	auto lhsSymbol = ts_node_symbol(lhs);
	Type* lhsType = nullptr;

	if (lhsSymbol == g_constants.memberAccess)
	{
		lhsType = EvaluateMemberAccess(lhs, fileScope, buffer);
	}
	else
	{
		if (auto decl = GetDeclarationForNode(lhs, fileScope, buffer))
		{
			lhsType = decl.value().type;
		}
	}

	if (!lhsType)
		return nullptr;

	auto rhs = ts_node_named_child(node, 1);
	auto rhsHash = GetIdentifierHash(rhs, buffer);

	if (auto rhsDecl = lhsType->members->TryGet(rhsHash))
	{
		return rhsDecl.value().type;
	}

	return nullptr;
}


Type* GetTypeForNode(TSNode node, FileScope* fileScope, GapBuffer* buffer)
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
	if (nodeSymbol == g_constants.memberAccess)
	{
		return EvaluateMemberAccess(node, fileScope, buffer);
	}

	// identifier or something worse!

	auto parent = ts_node_parent(node);
	bool inFileScope = false;
	bool terminalLHS = false;

	while (!fileScope->scopes.contains(parent.id))
	{
		if (ts_node_is_null(parent))
		{
			inFileScope = true;
			break;
		}

		if (!terminalLHS)
		{
			auto parentSymbol = ts_node_symbol(parent);
			if (parentSymbol == g_constants.memberAccess)
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
	if(!inFileScope)
		scope = &fileScope->scopes[parent.id];

	auto decl = GetDeclarationForNodeFromScope(node, fileScope, buffer, scope, parent);
	if (decl)
	{
		return decl.value().type;
	}

	return nullptr;
}


export const char* Hover(Hash documentName, int row, int col)
{
	auto tree = ts_tree_copy(g_trees.Read(documentName).value());
	auto root = ts_tree_root_node(tree);
	auto buffer = g_buffers.Read(documentName).value();
	auto fileScope = g_fileScopes.Read(documentName).value();

	auto point = TSPoint{ static_cast<uint32_t>(row), static_cast<uint32_t>(col) };
	auto node = ts_node_named_descendant_for_point_range(root, point, point);
	auto type = GetTypeForNode(node, fileScope, buffer);

	if (type)
	{
		ts_tree_delete(tree);
		return type->name;
	}

	ts_tree_delete(tree);
	return nullptr;
}