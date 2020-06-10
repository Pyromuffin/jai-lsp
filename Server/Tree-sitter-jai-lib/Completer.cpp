#include "TreeSitterJai.h"



export const char* GetCompletionItems(Hash documentHash, int row, int col)
{
	static std::string str;
	str.clear();

	auto tree = ts_tree_copy(g_trees.Read(documentHash).value());
	auto root = ts_tree_root_node(tree);
	auto buffer = g_buffers.Read(documentHash).value();
	auto fileScope = g_fileScopes.Read(documentHash).value();

	auto point = TSPoint{ static_cast<uint32_t>(row), static_cast<uint32_t>(col - 1) }; // be careful not to underflow!
	auto node = ts_node_named_descendant_for_point_range(root, point, point);
	
	if (ts_node_has_error(node))
	{
		auto child = ts_node_named_child(node, 0);
		if (!ts_node_is_null(child))
			node = child;
	}

	auto type = GetTypeForNode(node, fileScope, buffer);

	if (!type)
	{
		auto scope = GetScopeForNode(node, fileScope);
		if (scope)
		{
			auto members = scope->entries;
			for (auto& kvp : members)
			{
				str.append(kvp.second.name);
				str.append(",");
			}

			return str.c_str();
		}

		return nullptr;
	}

	auto members = type->members;
	if (members == nullptr)
		return nullptr;

	for ( auto& kvp : members->entries)
	{
		str.append(kvp.second.name);
		str.append(",");
	}

	return str.c_str();
}
