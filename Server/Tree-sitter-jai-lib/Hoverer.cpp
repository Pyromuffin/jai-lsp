#include "TreeSitterJai.h"

bool GetDeclarationForNode(TSNode node, GapBuffer* buffer, FileScope* fileScope, ScopeDeclaration* outDecl)
{
	auto identifierHash = GetIdentifierHash(node, buffer);
	auto scope = GetScopeForNode(node, fileScope);

	if (scope != nullptr && scope->entries.contains(identifierHash))
	{
		*outDecl = scope->entries[identifierHash];
		return true;
	}
	else
	{
		// search file scope
		if (fileScope->file.entries.contains(identifierHash))
		{
			*outDecl = fileScope->file.entries[identifierHash];
			return true;
		}

		// search modules
		for (int i = 0; i < fileScope->imports.size(); i++)
		{
			auto importHash = fileScope->imports[i];
			auto moduleScope = &g_modules[importHash];
			if (moduleScope->entries.contains(identifierHash))
			{
				moduleScope->entries[identifierHash]; //// ehhh
				auto moduleEntry = moduleScope->entries[identifierHash];

				// fix this garbage.
				ScopeDeclaration entry;
				entry.name = moduleEntry.name;
				entry.definitionNode = moduleEntry.definitionNode;
				entry.tokenType = moduleEntry.tokenType;
				entry.type = moduleEntry.type;

				*outDecl = entry;
				return true;
			}
		}
	}

	return false;
}


export const char* Hover(Hash documentName, int row, int col)
{
	auto tree = ts_tree_copy(g_trees[documentName]);
	auto root = ts_tree_root_node(tree);
	auto buffer = &g_buffers[documentName];
	auto fileScope = &g_fileScopes[documentName];

	auto point = TSPoint{ static_cast<uint32_t>(row), static_cast<uint32_t>(col) };

	auto node = ts_node_named_descendant_for_point_range(root, point, point);
	
	ScopeDeclaration decl;
	if (GetDeclarationForNode(node, buffer, fileScope, &decl))
	{
		ts_tree_delete(tree);

		if (decl.type != nullptr)
			return decl.type->name;
		else
			return nullptr;
	}


	ts_tree_delete(tree);
	return nullptr;
}