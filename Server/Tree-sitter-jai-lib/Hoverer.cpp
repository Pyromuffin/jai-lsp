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

	if (handle.fileIndex == UINT16_MAX && handle.index != UINT16_MAX)
	{
		// built in type.
		return &g_constants.builtInTypesByIndex[handle.index];
	}

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

		scope = GetScopeAndParentForNode(parent, fileScope, &parent); // aliasing???
	}

	return fileScope->Search(identifierHash);
}


std::optional<ScopeDeclaration> GetDeclarationForTopLevelNode(TSNode node, FileScope* fileScope, const GapBuffer* buffer)
{
	TSNode parent;
	auto scope = GetScopeAndParentForNode(node, fileScope, &parent);
	return GetDeclarationForNodeFromScope(node, fileScope, buffer, scope, parent);
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
		if (auto rhsDecl = typeKing->members->TryGet(rhsHash))
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
	if(!inFileScope)
		scope = &fileScope->scopes[parent.id];

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


export const char* Hover(uint64_t hashValue, int row, int col)
{
	auto documentName = Hash{ .value = hashValue };

	auto tree = ts_tree_copy(g_trees.Read(documentName).value());
	auto root = ts_tree_root_node(tree);
	auto buffer = g_buffers.Read(documentName).value();
	auto fileScope = g_fileScopes.Read(documentName).value();

	auto point = TSPoint{ static_cast<uint32_t>(row), static_cast<uint32_t>(col) };
	auto node = ts_node_named_descendant_for_point_range(root, point, point);
	
	if (auto type = GetTypeForNode(node, fileScope, buffer))
	{
		ts_tree_delete(tree);
		return type->name.c_str();
	}


	ts_tree_delete(tree);
	return nullptr;
}