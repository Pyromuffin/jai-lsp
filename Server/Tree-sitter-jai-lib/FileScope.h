#pragma once
#include "TreeSitterJai.h"
#include <assert.h>



struct ScopeStack
{
	std::vector<Scope*> scopes;
};



struct FileScope
{
	Hash documentHash;
	uint16_t fileIndex;

	std::vector<Hash> imports;
	std::vector<Hash> loads;
	std::vector<TypeKing> types;
	std::unordered_map<const void*, Scope> scopes;
	std::vector<SemanticToken> tokens;
	const GapBuffer* buffer;
	Scope file;

	Cursor scope_builder_cursor;

	void Clear()
	{
		imports.clear();
		loads.clear();
		types.clear();
		scopes.clear();
		tokens.clear();
		file.Clear();
	}


	//  oooooo kkkkk ay
	// i realize now that imports CAN be exported and we probably need to account for this.
	// this is evidenced by the fact that sometimes imports are declared in file scope, presumably so they don't leak out to the module.

	std::optional<ScopeDeclaration> SearchExports(Hash identifierHash);
	std::optional<ScopeDeclaration> SearchModules(Hash identifierHash);
	std::optional<ScopeDeclaration> Search(Hash identifierHash);



	TypeHandle AllocateType()
	{
		types.push_back(TypeKing());
		return TypeHandle{ .fileIndex = fileIndex, .index = static_cast<uint16_t>(types.size() - 1) };
	}

	const TypeKing* GetType(TypeHandle handle) const
	{
		assert(handle != TypeHandle::Null());

		if (handle.fileIndex == UINT16_MAX && handle.index != UINT16_MAX) // prooobably make the built in types just their own file scope so we dont have to do these checks.
		{
			// built in type.
			return &g_constants.builtInTypesByIndex[handle.index];
		}

		if(handle.fileIndex == fileIndex)
			return &types[handle.index];

		return &g_fileScopeByIndex[handle.fileIndex]->types[handle.index];
	}


	void HandleMemberReference(TSNode rhsNode, ScopeStack& stack, std::vector<TSNode>& unresolvedEntry, std::vector<int>& unresolvedTokenIndex);
	void HandleVariableReference(TSNode node, ScopeStack& stack, std::vector<TSNode>& unresolvedEntry, std::vector<int>& unresolvedTokenIndex);
	void HandleNamedDecl(const TSNode nameNode, Scope* currentScope, std::vector<std::tuple<Hash, TSNode>>& unresolvedTypes, std::vector<std::pair<TSNode, TypeHandle>>& structs, bool exporting);
	void FindDeclarations(TSNode scopeNode, Scope* scope, TypeHandle handle, ScopeStack& stack, bool& exporting);
	void HandleFunctionDefnitionParameters(TSNode node, Scope* currentScope, TypeHandle handle, std::vector<std::tuple<Hash, TSNode>>& unresolvedNodes, Cursor& cursor);
	TypeHandle HandleFuncDefinitionNode(TSNode node, std::vector<std::pair<TSNode, TypeHandle>>& structs, DeclarationFlags& flags);
	void CreateTopLevelScope(TSNode node, ScopeStack& stack, bool& exporting);
	void Build();

	const std::optional<TypeHandle> EvaluateNodeExpressionType(TSNode node, const GapBuffer* buffer, Scope* current, ScopeStack& stack, FileScope* fileScope);



};
