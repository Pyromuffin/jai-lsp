#include "TreeSitterJai.h"

struct Range
{
	int startRow, startCol;
	int endRow, endCol;
};

static Range PointsToRange(TSPoint start, TSPoint end)
{
	Range range;
	range.startCol = start.column;
	range.startRow = start.row;
	range.endCol = end.column;
	range.endRow = end.row;

	return range;
}

static Range NodeToRange(TSNode node)
{
	auto start = ts_node_start_point(node);
	auto end = ts_node_end_point(node);
	return PointsToRange(start, end);
}


export void FindDefinition(Hash documentName, int row, int col, Hash* outFileHash, Range* outOriginRange, Range* outTargetRange, Range* outSelectionRange)
{
	auto tree = ts_tree_copy(g_trees.Read(documentName).value());
	auto root = ts_tree_root_node(tree);
	auto buffer = g_buffers.Read(documentName).value();;
	auto fileScope = g_fileScopes.Read(documentName).value();

	auto point = TSPoint{ static_cast<uint32_t>(row), static_cast<uint32_t>(col) };

	auto identifierNode = ts_node_named_descendant_for_point_range(root, point, point);
	auto identifierHash = GetIdentifierHash(identifierNode, buffer);
	TSNode parent;
	auto scope = GetScopeAndParentForNode(identifierNode, fileScope, &parent);

	*outOriginRange = NodeToRange(identifierNode);

	if (scope != nullptr && scope->entries.contains(identifierHash))
	{
		auto entry = scope->entries[identifierHash];
		*outFileHash = documentName;
		*outTargetRange = NodeToRange(parent);
		*outSelectionRange = NodeToRange(entry.definitionNode);
		ts_tree_delete(tree);
		return;
	}
	else
	{
		// search file scope
		if (fileScope->file.entries.contains(identifierHash))
		{
			auto entry = fileScope->file.entries[identifierHash];
			*outFileHash = documentName;
			*outTargetRange = NodeToRange(entry.definitionNode);
			*outSelectionRange = NodeToRange(entry.definitionNode);
			ts_tree_delete(tree);
			return;
		}


		// search modules
		for (int i = 0; i < fileScope->imports.size(); i++)
		{
			auto importHash = fileScope->imports[i];
			auto moduleScope = g_modules.Read(importHash).value();
			if (moduleScope->entries.contains(identifierHash))
			{
				auto entry = moduleScope->entries[identifierHash];
				*outFileHash = entry.definingFile;
				*outTargetRange = NodeToRange(entry.definitionNode);
				*outSelectionRange = NodeToRange(entry.definitionNode);
				ts_tree_delete(tree);
				return;
			}
		}
	}


	*outFileHash = Hash{ 0 };
	ts_tree_delete(tree);
}