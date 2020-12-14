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

int GetDeclarationForNodeFromScope(TSNode node, FileScope* fileScope, Scope* scope, FileScope** outFile, Scope** outScope)
{
	// search scopes going up for entries, if they're data scopes. if they're imperative scopes then declarations have to be in order.
	auto identifierHash = GetIdentifierHash(node, fileScope->buffer);
	while (scope != nullptr)
	{
		auto declIndex = scope->GetIndex(identifierHash);
		if (declIndex >= 0)
		{
			// if the scope is imperative, then respect ordering, if it is not, just get the decl.
			if (scope->imperative)
			{
				auto decl = scope->GetDeclFromIndex(declIndex);
				auto constant = decl->HasFlags(DeclarationFlags::Constant);

				auto definitionStart = decl->startByte;
				auto identifierStart = ts_node_start_byte(node);
				if (constant || (identifierStart >= definitionStart))
				{
					*outFile = fileScope;
					*outScope = scope;
					return declIndex;
				}
			}
			else
			{
				*outFile = fileScope;
				*outScope = scope;
				return declIndex;
			}
		}

		scope = fileScope->GetScope(scope->parent);
	}
	

	return fileScope->SearchAndGetModule(identifierHash, outFile, outScope);
}


TSNode ConstructRhsFromDecl(ScopeDeclaration decl, TSTree* tree);

static inline std::optional<ScopeDeclaration> Evaluate(ScopeDeclaration* decl)
{

}

int EvaluateMemberAccess(TSNode node, FileScope* fileScope, Scope* scope, Scope** outScope, FileScope** outFile)
{
	// rhs should always be an identifier ?

	auto lhs = ts_node_named_child(node, 0);
	auto lhsSymbol = ts_node_symbol(lhs);
	ScopeDeclaration* lhsDecl = nullptr;
	int declIndex = -1;

	if (IsMemberAccess(lhsSymbol))
	{
		declIndex = EvaluateMemberAccess(lhs, fileScope, scope, outScope, outFile);
		if (declIndex >= 0)
			lhsDecl = (*outScope)->GetDeclFromIndex(declIndex);
	}
	else
	{
		declIndex = GetDeclarationForNodeFromScope(lhs, fileScope, scope, outFile, outScope);
		if (declIndex >= 0)
			lhsDecl = (*outScope)->GetDeclFromIndex(declIndex);
	}

	if (!lhsDecl)
		return -1;

	if ( !lhsDecl->HasFlags(DeclarationFlags::Evaluated) )
	{
		// so we need to get the file scope for this declaration as well

		auto rhsNode = ConstructRhsFromDecl(*lhsDecl, (*outFile)->currentTree);
		if (auto typeHandle = (*outFile)->EvaluateNodeExpressionType(rhsNode, *outScope))
		{
			lhsDecl->type = *typeHandle;
			lhsDecl->flags = lhsDecl->flags | DeclarationFlags::Evaluated;
		}
		else
		{
			return -1;
		}
	}



	if (ts_node_symbol(node) == g_constants.memberAccessNothing)
		return declIndex;

	auto rhs = ts_node_named_child(node, 1);
	auto rhsHash = GetIdentifierHash(rhs, fileScope->buffer);

	// search for rhs in the members of the LHS type

	auto file = g_fileScopeByIndex.Read(lhsDecl->type.fileIndex);
	auto members = file->GetScope(lhsDecl->type.scope);
	if (!members->checked)
	{
		file->CheckScope(members);
	}

	declIndex = members->GetIndex(rhsHash);
	if (declIndex >= 0)
	{
		*outScope = members;
		*outFile = file;
		return declIndex;
	}

	return -1;
}


std::optional<TypeHandle> EvaluateMemberAccessType(TSNode node, FileScope* fileScope, Scope* scope)
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
		auto members = file->GetScope(lhsType->scope);
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

// starting scope can be nullptr.
int GetDeclarationForNode(TSNode node, FileScope* fileScope, Scope* startingScope, FileScope** outFile, Scope** outScope)
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

	if (startingScope == nullptr)
	{
		if (auto scope = GetScopeForNode(node, fileScope))
		{
			startingScope = fileScope->GetScope(*scope);
		}
	}


	auto nodeSymbol = ts_node_symbol(node);
	if (IsMemberAccess(nodeSymbol))
	{
		return EvaluateMemberAccess(node, fileScope, startingScope, outScope, outFile);
	}

	// identifier or something worse!
	auto parent = ts_node_parent(node);
	auto parentSymbol = ts_node_symbol(parent);

	if (IsMemberAccess(parentSymbol))
	{
		auto lhs = ts_node_named_child(parent, 0);
		if (lhs.id == node.id)
		{
			return GetDeclarationForNodeFromScope(node, fileScope, startingScope, outFile, outScope);
		}
		else
		{
			return EvaluateMemberAccess(parent, fileScope, startingScope, outScope, outFile);
		}
	}

	// @TODO make sure this isn't broken
	// this should probably? always be evalualted because this only runs in the tokens code.
	auto result = GetDeclarationForNodeFromScope(node, fileScope, startingScope, outFile, outScope);
#if _DEBUG
	if (result >= 0)
	{
		auto decl = (*outScope)->GetDeclFromIndex(result);
		assert(decl->HasFlags(DeclarationFlags::Evaluated));
	}
#endif

	return result;
	/*
	auto declIndex = GetDeclarationForNodeFromScope(node, fileScope, startingScope, outFile, outScope);
	if (declIndex >= 0)
	{
		auto decl = (*outScope)->GetDeclFromIndex(declIndex);

		if (!decl->HasFlags(DeclarationFlags::Evaluated))
		{
			auto rhsNode = ConstructRhsFromDecl(*decl, (*outFile)->currentTree);
			if (auto typeHandle = (*outFile)->EvaluateNodeExpressionType(rhsNode, *outScope))
			{
				decl->type = *typeHandle;
				decl->flags = decl->flags | DeclarationFlags::Evaluated;
				return declIndex;
			}
		}
		else
		{
			return declIndex;
		}
	}

	return -1;
	*/
}


const std::optional<TypeHandle> GetTypeForNode(TSNode node, FileScope* file)
{
	// first is to get the scope for the node.
	if (auto scopehandle = GetScopeForNode(node, file))
	{
		auto scope = file->GetScope(*scopehandle);
		auto nodeSymbol = ts_node_symbol(node);

		if (IsMemberAccess(nodeSymbol))
		{
			return EvaluateMemberAccessType(node, file, scope);
		}

		// identifier or something worse!
		auto parent = ts_node_parent(node);
		if (ts_node_is_null(parent))
		{
			return std::nullopt;
		}

		auto parentSymbol = ts_node_symbol(parent);

		if (IsMemberAccess(parentSymbol))
		{
			auto lhs = ts_node_named_child(parent, 0);
			if (lhs.id == node.id)
			{
				return file->EvaluateNodeExpressionType(node, scope);
			}
			else
			{
				return EvaluateMemberAccessType(parent, file, scope);
			}
		}

		return  file->EvaluateNodeExpressionType(node, scope);
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


static void GetAttributeText(std::string& dst, TypeHandle handle)
{
	auto file = g_fileScopeByIndex.Read(handle.fileIndex);
	auto king = &file->types[handle.index];
	dst = "";

	for (int i = 0; i < 8; i++)
	{
		auto attr = handle.GetAttribute(7 - i );
		if (attr == TypeAttribute::none)
			continue;
		else if (attr == TypeAttribute::pointerTo)
			dst.append("*");
		else if (attr == TypeAttribute::arrayOf)
			dst.append("[] ");
	}

	dst.append(king->name);
}


export_jai_lsp const char* Hover(uint64_t hashValue, int row, int col)
{
	static std::string hoverText;
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
		ts_tree_delete(tree);
		GetAttributeText(hoverText, *type);
		return hoverText.c_str();
	}


	ts_tree_delete(tree);
	return nullptr;
}
