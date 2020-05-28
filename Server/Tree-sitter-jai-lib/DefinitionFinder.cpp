#include "TreeSitterJai.h"

export void FindDefinition(Hash documentName, int row, int col, Hash* outFileHash, int* outRow, int* outCol)
{
	auto tree = ts_tree_copy(g_trees[documentName]);
	auto root = ts_tree_root_node(tree);
	auto buffer = &g_buffers[documentName];
	auto fileScope = &g_fileScopes[documentName];

	auto point = TSPoint{ static_cast<uint32_t>(row), static_cast<uint32_t>(col) };

	auto identifierNode = ts_node_named_descendant_for_point_range(root, point, point);
	auto identifierHash = GetIdentifierHash(identifierNode, buffer);
	auto scope = GetScopeForNode(identifierNode, fileScope);

	if (scope != nullptr)
	{
		auto entry = scope->entries[identifierHash];
		*outFileHash = documentName;
		*outRow = entry.definitionPosition.row;
		*outCol = entry.definitionPosition.column;
		ts_tree_delete(tree);
		return;
	}
	else
	{
		// search modules
		for (int i = 0; i < fileScope->imports.size(); i++)
		{
			auto importHash = fileScope->imports[i];
			auto moduleScope = &g_modules[importHash];
			if (moduleScope->entries.contains(identifierHash))
			{
				auto entry = moduleScope->entries[identifierHash];
				*outFileHash = importHash;
				*outRow = entry.definitionPosition.row;
				*outCol = entry.definitionPosition.column;
				ts_tree_delete(tree);
				return;
			}
		}
	}


	*outFileHash = Hash{ 0 };
	ts_tree_delete(tree);
}