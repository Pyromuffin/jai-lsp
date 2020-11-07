#pragma once
#include "TreeSitterJai.h"

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

	std::optional<ScopeDeclaration> GetRightHandSideDecl(TSNode lhsDeclNode, Hash rhsHash, std::vector<Scope*>& scopeKing);



	TypeHandle AllocateType()
	{
		types.push_back(TypeKing());
		return TypeHandle{ .fileIndex = fileIndex, .index = static_cast<uint16_t>(types.size() - 1) };
	}

	const TypeKing* GetType(TypeHandle handle) const
	{
		return &types[handle.index];
	}


	void HandleMemberReference(TSNode lhsNode, TSNode rhsNode, std::vector<Scope*>& scopeKing, std::vector<TSNode>& unresolvedEntry, std::vector<int>& unresolvedTokenIndex, std::unordered_map<Hash, TSNode>& parameters);
	void HandleVariableReference(TSNode node, std::vector<Scope*>& scopeKing, std::vector<TSNode>& unresolvedEntry, std::vector<int>& unresolvedTokenIndex, std::unordered_map<Hash, TSNode>& parameters);
	void HandleNamedDecl(const TSNode nameNode, Scope* currentScope, std::vector<std::tuple<Hash, TSNode>>& unresolvedTypes, std::vector<Scope*>& scopeKing, std::vector<TSNode>& structs, bool exporting);
	void FindDeclarations(TSNode scopeNode, Scope* scope, std::vector<Scope*>& scopeKing, bool& exporting);
	void CreateScope(TSNode& node, std::unordered_map<Hash, TSNode>* parameters, std::vector<Scope*>& scopeKing, bool imperative, bool& exporting);
	void CreateTopLevelScope(TSNode node, std::vector<Scope*>& scopeKing, bool& exporting);
	void Build();

	const std::optional<TypeHandle> EvaluateNodeExpressionType(TSNode node, const GapBuffer* buffer, Scope* current, std::vector<Scope*>& scopeKing, FileScope* fileScope);


};
