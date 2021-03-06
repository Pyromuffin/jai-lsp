#include "TreeSitterJai.h"
#include "FileScope.h"

TSNode ConstructRhsFromDecl(ScopeDeclaration decl, TSTree* tree);


enum InvocationType
{
	Invoked = 1,
	TriggerCharacter = 2,
	TriggerForIncompleteCompletions = 3
};


export_jai_lsp const char* GetCompletionItems(uint64_t hashValue, int row, int col, InvocationType invocation)
{
	auto documentHash = Hash{ .value = hashValue };

	static std::string str;
	str.clear();

	auto tree = ts_tree_copy(g_trees.Read(documentHash).value());
	auto root = ts_tree_root_node(tree);
	auto buffer = g_buffers.Read(documentHash).value();
	auto fileScope = g_fileScopes.Read(documentHash).value();

	if (fileScope->status == FileScope::Status::scopesBuilt)
	{
		fileScope->WaitForDependencies();
		fileScope->DoTypeCheckingAndInference(tree);

		fileScope->status = FileScope::Status::checked;
	}
	else if (fileScope->status != FileScope::Status::checked)
	{
		return nullptr;
	}




	auto point = TSPoint{ static_cast<uint32_t>(row), static_cast<uint32_t>(col) };
	if (invocation == TriggerCharacter)
		point.column--; // this is so that we try to get the node behind the "."
						// ideally we would skip whitespace and try to find the "nearest" node.
						// also underflow may be possible ?? maybe ???

	auto node = ts_node_named_descendant_for_point_range(root, point, point);

	/*
	if (ts_node_has_error(node))
	{
		auto child = ts_node_named_child(node, 0);
		if (!ts_node_is_null(child))
			node = child;
	}
	*/

	auto symbol = ts_node_symbol(node);
	if (symbol == g_constants.imperativeScope || symbol == g_constants.dataScope)
	{
		auto parent = ts_node_parent(node);
		auto parentSymbol = ts_node_symbol(parent);
		if (parentSymbol == g_constants.funcDefinition || parentSymbol == g_constants.enumDecl || parentSymbol == g_constants.structDecl|| parentSymbol == g_constants.unionDecl)
		{
			node = parent;
		}
	}


	bool sourceFile = ts_node_symbol(node) == g_constants.sourceFile;
	if (fileScope->ContainsScope(node.id) || sourceFile)
	{
		// we've invoked this on some empty space in a scope, so lets just print out whatever is in scope up to this point.

		ScopeHandle scopeHandle;
		if (sourceFile)
			scopeHandle = fileScope->file;
		else
			scopeHandle = fileScope->GetScopeFromNodeID(node.id);

		auto scope = fileScope->GetScope(scopeHandle);
		
		

		while (scope)
		{
			scope->AppendMembers(str, buffer);
			scope = fileScope->GetScope(scope->parent);
		}

		/*
		// if invoked in a scope, just return after we've gotten everything for just our file. We likely don't want imports at this point.
		if (invocation == Invoked)
		{
			return str.c_str();
		}
		*/

		// and append loads
		for (auto load : fileScope->loads)
		{
			if (auto loadedScope = g_fileScopes.Read(load))
			{
				if ((*loadedScope)->fileIndex == 0) // skip built in scope.
					continue;

				auto loadedBuffer = g_buffers.Read(load).value();
				(*loadedScope)->GetScope((*loadedScope)->file)->AppendExportedMembers(str, loadedBuffer);
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
				moduleFile->GetScope(moduleFile->file)->AppendExportedMembers(str, importedBuffer);

				// and double finally append all exported member for each loaded file.
				for (auto load : moduleFile->loads)
				{
					if (auto loadedScope = g_fileScopes.Read(load))
					{
						if ((*loadedScope)->fileIndex == 0) // skip built in scope.
							continue;

						auto loadedBuffer = g_buffers.Read(load).value();
						(*loadedScope)->GetScope((*loadedScope)->file)->AppendExportedMembers(str, loadedBuffer);
					}
				}
			}
		}

		return str.c_str();
	}


	// if we're not in a scope, then try to get the type of the node we're completing
	// such as a member access expression.
	
	if (auto typeHandle = GetTypeForNode(node, fileScope))
	{
		if (auto type = GetType(*typeHandle) )
		{
			auto memberScope = g_fileScopeByIndex.Read(typeHandle->fileIndex)->GetScope(typeHandle->scope);
			if (memberScope == nullptr)
				return nullptr;
	
			auto bufferForType = g_fileScopeByIndex.Read(typeHandle->fileIndex)->buffer;
			memberScope->AppendMembers(str, bufferForType);

			return str.c_str();
		}

		return nullptr;
	}
	else if (invocation == TriggerCharacter)
	{
		// we pressed "." on a thing which we couldn't get the type of, so just return nothing;
		return nullptr;
	}
	else 
	{
		TSNode parent;
		ScopeHandle scope;
		auto found = GetScopeAndParentForNode(node, fileScope, &parent, &scope);
		if (found)
		{
			fileScope->GetScope(scope)->AppendMembers(str, buffer);
			TSNode scopeScopeParent;
			ScopeHandle scopeScope;
			found = GetScopeAndParentForNode(parent, fileScope, &scopeScopeParent, &scopeScope);
			bool foundAnything = found;

			while (found)
			{
				fileScope->GetScope(scopeScope)->AppendMembers(str, buffer);
				found = GetScopeAndParentForNode(scopeScopeParent, fileScope, &scopeScopeParent, &scopeScope);
			}

			// and append whatever is in file scope for good measure
			if(!foundAnything)
				fileScope->GetScope(fileScope->file)->AppendMembers(str, buffer);

			// and append loads
			for (auto load : fileScope->loads)
			{
				if (auto loadedScope = g_fileScopes.Read(load))
				{
					if ((*loadedScope)->fileIndex == 0) // skip built in scope.
						continue;

					auto loadedBuffer = g_buffers.Read(load).value();
					(*loadedScope)->GetScope((*loadedScope)->file)->AppendExportedMembers(str, loadedBuffer);
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
					moduleFile->GetScope(moduleFile->file)->AppendExportedMembers(str, importedBuffer);

					// and double finally append all exported member for each loaded file.
					for (auto load : moduleFile->loads)
					{
						if (auto loadedScope = g_fileScopes.Read(load))
						{
							if ((*loadedScope)->fileIndex == 0) // skip built in scope.
								continue;

							auto loadedBuffer = g_buffers.Read(load).value();
							(*loadedScope)->GetScope((*loadedScope)->file)->AppendExportedMembers(str, loadedBuffer);
						}
					}


				}
			}


			return str.c_str();
		}

		return nullptr;
	}

	
}
