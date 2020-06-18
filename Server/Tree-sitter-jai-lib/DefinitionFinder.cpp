#include "TreeSitterJai.h"
#include "FileScope.h"

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
	std::optional<ScopeDeclaration> entry = std::nullopt;

	// try something real simple


	if (scope != nullptr)
	{
		entry = scope->TryGet(identifierHash);
	}

	if (entry)
	{
		*outFileHash = documentName;
		*outTargetRange = NodeToRange(parent);
		// wow this is dumb but we're going to query the tree to get the node for the declaration offset! hope that offset isn't stale!
		auto definitionNode = ts_node_named_descendant_for_byte_range(root, entry.value().startByte, entry.value().startByte + entry.value().length);
		*outSelectionRange = NodeToRange(definitionNode);
		ts_tree_delete(tree);
		return;
	}
	else
	{
		// search file scope
		if (auto decl = fileScope->file.TryGet(identifierHash))
		{
			auto entry = decl.value();
			*outFileHash = documentName;
			auto definitionNode = ts_node_named_descendant_for_byte_range(root, entry.startByte, entry.startByte + entry.length);
			*outTargetRange = NodeToRange(definitionNode);
			*outSelectionRange = NodeToRange(definitionNode);
			ts_tree_delete(tree);
			return;
		}


		for (auto load : fileScope->loads)
		{
			if (auto loadedScope = g_fileScopes.Read(load))
			{
				if (auto decl = loadedScope.value()->file.TryGet(identifierHash))
				{
					if (!(decl.value().flags & DeclarationFlags::Exported))
						continue;

					auto entry = decl.value();
					*outFileHash = load;

					auto loadedTree = ts_tree_copy(g_trees.Read(load).value());
					auto loadedRoot = ts_tree_root_node(loadedTree);
					auto definitionNode = ts_node_named_descendant_for_byte_range(loadedRoot, entry.startByte, entry.startByte + entry.length);
					*outTargetRange = NodeToRange(definitionNode);
					*outSelectionRange = NodeToRange(definitionNode);

					ts_tree_delete(loadedTree);
					ts_tree_delete(tree);
					return;
				}
			}
		}

		// search modules the slooow way.
		// this doesnt work for multiple layers of loads btw.
		for (int i = 0; i < fileScope->imports.size(); i++)
		{
			auto importHash = fileScope->imports[i];
			auto moduleScope = g_modules.Read(importHash).value();

			if (auto decl = moduleScope->moduleFile->file.TryGet(identifierHash))
			{
				auto entry = decl.value();
				if (!(entry.flags & DeclarationFlags::Exported))
					continue;

				auto loadedTree = ts_tree_copy(g_trees.Read(moduleScope->moduleFileHash).value());
				auto loadedRoot = ts_tree_root_node(loadedTree);

				*outFileHash = moduleScope->moduleFileHash;
				auto definitionNode = ts_node_named_descendant_for_byte_range(loadedRoot, entry.startByte, entry.startByte);
				*outTargetRange = NodeToRange(definitionNode);
				*outSelectionRange = NodeToRange(definitionNode);

				ts_tree_delete(tree);
				ts_tree_delete(loadedTree);
				return;
			}



			for (auto load : moduleScope->moduleFile->loads)
			{
				if (auto loadedScope = g_fileScopes.Read(load))
				{
					if (auto decl = loadedScope.value()->file.TryGet(identifierHash))
					{
						auto entry = decl.value();
						if (!(entry.flags & DeclarationFlags::Exported))
							continue;

						auto loadedTree = ts_tree_copy(g_trees.Read(load).value());
						auto loadedRoot = ts_tree_root_node(loadedTree);

						*outFileHash = load;
						auto definitionNode = ts_node_named_descendant_for_byte_range(loadedRoot, entry.startByte, entry.startByte);
						*outTargetRange = NodeToRange(definitionNode);
						*outSelectionRange = NodeToRange(definitionNode);
						
						ts_tree_delete(tree);
						ts_tree_delete(loadedTree);
						return;
					}
				}
			}
		}
	}


	*outFileHash = Hash{ 0 };
	ts_tree_delete(tree);
}