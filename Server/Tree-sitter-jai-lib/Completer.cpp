#include "TreeSitterJai.h"
#include "FileScope.h"

enum InvocationType
{
	Invoked = 1,
	TriggerCharacter = 2,
	TriggerForIncompleteCompletions = 3
};


export const char* GetCompletionItems(Hash documentHash, int row, int col, InvocationType invocation)
{
	static std::string str;
	str.clear();

	auto tree = ts_tree_copy(g_trees.Read(documentHash).value());
	auto root = ts_tree_root_node(tree);
	auto buffer = g_buffers.Read(documentHash).value();
	auto fileScope = g_fileScopes.Read(documentHash).value();

	
	auto point = TSPoint{ static_cast<uint32_t>(row), static_cast<uint32_t>(col) };
	if (invocation == TriggerCharacter)
		point.column--; // this is so that we try to get the node behind the "."
						// ideally we would skip whitespace and try to find the "nearest" node.
						// also underflow may be possible ?? maybe ???

	auto node = ts_node_named_descendant_for_point_range(root, point, point);
	
	if (ts_node_has_error(node))
	{
		auto child = ts_node_named_child(node, 0);
		if (!ts_node_is_null(child))
			node = child;
	}

	if (fileScope->scopes.contains(node.id))
	{
		// we've invoked this on some empty space in a scope, so lets just print out whatever is in scope up to this point.
		auto scope = &fileScope->scopes[node.id];
		scope->AppendMembers(str, buffer);

		TSNode scopeScopeParent;
		auto scopeScope = GetScopeAndParentForNode(node, fileScope, &scopeScopeParent);
		while (scopeScope)
		{
			scopeScope->AppendMembers(str, buffer);
			scopeScope = GetScopeAndParentForNode(scopeScopeParent, fileScope, &scopeScopeParent);
		}

		// and append whatever is in file scope for good measure:
		fileScope->file.AppendMembers(str, buffer);

		// and append loads
		for (auto load : fileScope->loads)
		{
			if (auto loadedScope = g_fileScopes.Read(load))
			{
				auto loadedBuffer = g_buffers.Read(load).value();
				loadedScope.value()->file.AppendExportedMembers(str, loadedBuffer);
			}
		}

		// and finally append imports.
		for (auto import : fileScope->imports)
		{
			if (auto mod = g_modules.Read(import))
			{
				auto moduleFile = mod.value()->moduleFile;
				auto mfHash = mod.value()->moduleFileHash;
				auto importedBuffer = g_buffers.Read(mfHash).value();
				moduleFile->file.AppendExportedMembers(str, importedBuffer);

				// and double finally append all exported member for each loaded file.
				for (auto load : moduleFile->loads)
				{
					if (auto loadedScope = g_fileScopes.Read(load))
					{
						auto loadedBuffer = g_buffers.Read(load).value();
						loadedScope.value()->file.AppendExportedMembers(str, loadedBuffer);
					}
				}
			}
		}

		return str.c_str();
	}


	// if we're not in a scope, then try to get the type of the node we're completing
	// such as a member access expression.
	
	if (auto decl = GetDeclarationForNode(node, fileScope, buffer))
	{
		if (auto type = GetType(decl->type))
		{
			if (type->members == nullptr)
				return nullptr;
	
			auto bufferForType = g_fileScopeByIndex[decl->type.fileIndex]->buffer;
			type->members->AppendMembers(str, bufferForType);

			return str.c_str();
		}

	}
	else
	{
		TSNode parent;
		auto scope = GetScopeAndParentForNode(node, fileScope, &parent);
		if (scope)
		{
			scope->AppendMembers(str, buffer);
			TSNode scopeScopeParent;
			auto scopeScope = GetScopeAndParentForNode(parent, fileScope, &scopeScopeParent);
			while (scopeScope)
			{
				scopeScope->AppendMembers(str, buffer);
				scopeScope = GetScopeAndParentForNode(scopeScopeParent, fileScope, &scopeScopeParent);
			}

			// and append whatever is in file scope for good measure:
			fileScope->file.AppendMembers(str, buffer);

			// and append loads
			for (auto load : fileScope->loads)
			{
				if (auto loadedScope = g_fileScopes.Read(load))
				{
					auto loadedBuffer = g_buffers.Read(load).value();
					loadedScope.value()->file.AppendExportedMembers(str, loadedBuffer);
				}
			}

			// and finally append imports.
			for (auto import : fileScope->imports)
			{
				if (auto mod = g_modules.Read(import))
				{
					auto moduleFile = mod.value()->moduleFile;
					auto mfHash = mod.value()->moduleFileHash;
					auto importedBuffer = g_buffers.Read(mfHash).value();
					moduleFile->file.AppendExportedMembers(str, importedBuffer);

					// and double finally append all exported member for each loaded file.
					for (auto load : moduleFile->loads)
					{
						if (auto loadedScope = g_fileScopes.Read(load))
						{
							auto loadedBuffer = g_buffers.Read(load).value();
							loadedScope.value()->file.AppendExportedMembers(str, loadedBuffer);
						}
					}


				}
			}


			return str.c_str();
		}

		return nullptr;
	}

	
}
