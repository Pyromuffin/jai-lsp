#pragma once
#include "TreeSitterJai.h"
#include "Hashmap.h"
#include <assert.h>


struct ScopeStack
{
	std::vector<ScopeHandle> scopes;
};



struct FileScope
{
	Hash documentHash;
	uint16_t fileIndex;

	std::vector<Hash> imports;
	std::vector<Hash> loads;
	std::vector<TypeKing> types;
	
	Hashmap offsetToHandle;

	std::vector<Scope> scopeKings;
	std::vector<ScopeHandle> scopeKingFreeList;

	std::vector<uint64_t> scopePresentBitmap[2];
	int whichBitmap = 0;

	//Hashmap<const void*, ScopeHandle>  idToHandle;
	std::unordered_map<const void*, ScopeHandle> _nodeToScopes;
	std::vector<SemanticToken> tokens;
	const GapBuffer* buffer;
	ScopeHandle file;

	Cursor scope_builder_cursor;

	TSInputEdit* edits;
	int editCount;

	std::vector<std::pair<ScopeHandle, TypeHandle>> usings;

	static constexpr bool INCREMENTAL_ANALYSIS = false;

	void Clear()
	{
		imports.clear();
		loads.clear();
		types.clear();
		_nodeToScopes.clear();
		tokens.clear();
		scopeKings.clear();
		scopeKingFreeList.clear();
		scopePresentBitmap[0].clear();
		scopePresentBitmap[1].clear();
		offsetToHandle.Clear();
	}


	//  oooooo kkkkk ay
	// i realize now that imports CAN be exported and we probably need to account for this.
	// this is evidenced by the fact that sometimes imports are declared in file scope, presumably so they don't leak out to the module.

	std::optional<ScopeDeclaration> SearchExports(Hash identifierHash);
	std::optional<ScopeDeclaration> SearchModules(Hash identifierHash);
	std::optional<ScopeDeclaration> Search(Hash identifierHash);

	Scope* GetScope(ScopeHandle handle)
	{
		return &scopeKings[handle.index];
	}

	void ClearScopePresentBits()
	{
		whichBitmap = (whichBitmap + 1) % 2;

		for (int i = 0; i < scopePresentBitmap[whichBitmap].size(); i++)
		{
			scopePresentBitmap[whichBitmap][i] = UINT64_MAX;
		}
	}

	void SetScopePresentBit(ScopeHandle handle)
	{
		auto index = handle.index;
		auto bitmapElement = index >> 6;
		auto bitIndex = index % 64;
		scopePresentBitmap[whichBitmap][bitmapElement] &= ~(1 << bitIndex);
	}

	bool ContainsScope(const void* id)
	{
		return _nodeToScopes.contains(id);
	}

	static int EditStartByte(int start_byte, int edit_start_byte, int new_end_byte, int old_end_byte)
	{
		if (start_byte >= old_end_byte) {
			start_byte = new_end_byte + (start_byte - old_end_byte);
		}
		else if (start_byte > edit_start_byte) { // between old end byte and edit start byte
			start_byte = -1;
		}

		return start_byte;
	}

	static int UnEditStartByte(int start_byte, int edit_start_byte, int new_end_byte, int old_end_byte)
	{
		return EditStartByte(start_byte, edit_start_byte, old_end_byte, new_end_byte);
	}

	static int UnEditStartByte(int start_byte, TSInputEdit* edits, int editCount)
	{
		auto edited = start_byte;
		for (int i = 0; i < editCount; i++)
		{
			edited = EditStartByte(start_byte, edits[i].start_byte, edits[i].old_end_byte, edits[i].new_end_byte);
		}

		return edited;
	}


	static void FixUpHashmapOffsets(Hashmap& hm, TSInputEdit* edits, int editCount)
	{
		auto kvps = hm.Data();
		auto entryCount = hm.Size();
		auto edit = edits[0];
		int deletedCount = 0;
		for (int i = 0; i < entryCount - deletedCount; i++)
		{
			auto kvp = kvps[i];
			auto editedOffset = kvp.key;
			for (int i = 0; i < editCount; i++)
			{
				editedOffset = EditStartByte(editedOffset, edits[i].start_byte, edits[i].new_end_byte, edits[i].old_end_byte);
			}

			// we need to detect deletions and remove them if we go down this route.

			if (kvp.key != editedOffset)
			{
				auto unedited = UnEditStartByte(editedOffset, edits[0].start_byte, edits[0].new_end_byte, edits[0].old_end_byte);

				hm.Remove(kvp.key);
				hm.Add(editedOffset, kvp.value);
				i--;
				deletedCount++;
			}
		}
	}


	bool ContainsScope(int offset, TSInputEdit* edits, int editCount)
	{
		for (int i = 0; i < editCount; i++)
		{
			offset = UnEditStartByte(offset, edits[i].start_byte, edits[i].new_end_byte, edits[i].old_end_byte);
		}
		return offsetToHandle.Contains(offset);
	}

	std::optional<ScopeHandle> GetScopeFromOffset(int offset, TSInputEdit* edits, int editCount)
	{
		for (int i = 0; i < editCount; i++)
		{
			offset = UnEditStartByte(offset, edits[i].start_byte, edits[i].new_end_byte, edits[i].old_end_byte);
		}

		int index = offsetToHandle.GetIndex(offset);

		if (index >= 0)
		{
			return offsetToHandle[index];
		}

		return std::nullopt;
	}


	ScopeHandle GetScopeFromNodeID(const void* id)
	{
		assert(ContainsScope(id));
		return _nodeToScopes[id];
	}


	std::optional<ScopeHandle> TryGetScopeFromNodeID(const void* id)
	{
		auto it = _nodeToScopes.find(id);
		if (it == _nodeToScopes.end())
			return std::nullopt;

		return it->second;
	}



	ScopeHandle AllocateScope(TSNode node)
	{
		if (scopeKingFreeList.size() > 0)
		{
			auto back = scopeKingFreeList.back();
			scopeKingFreeList.pop_back();
			_nodeToScopes[node.id] = back;
			GetScope(back)->Clear();

			return back;
		}

		scopeKings.push_back(Scope());
		auto numberOfBitwords = (scopeKings.size() >> 6) + 1;
		if (numberOfBitwords > scopePresentBitmap[0].size())
		{
			scopePresentBitmap[0].push_back(UINT64_MAX);
			scopePresentBitmap[1].push_back(UINT64_MAX);
		}

		auto handle = ScopeHandle{ .index = static_cast<uint16_t>(scopeKings.size() - 1) };
		_nodeToScopes[node.id] = handle;

		return handle;
	}


	TypeHandle AllocateType()
	{
		types.push_back(TypeKing());
		return TypeHandle{ .fileIndex = fileIndex, .index = static_cast<uint16_t>(types.size() - 1) };
	}

	const TypeKing* GetType(TypeHandle handle) const
	{
		assert(handle != TypeHandle::Null());

		if(handle.fileIndex == fileIndex)
			return &types[handle.index];

		return &g_fileScopeByIndex[handle.fileIndex]->types[handle.index];
	}

	std::optional<ScopeDeclaration> TryGet(Hash hash) const
	{
		return scopeKings[file.index].TryGet(hash);
	}


	void HandleMemberReference(TSNode rhsNode, ScopeStack& stack, std::vector<TSNode>& unresolvedEntry, std::vector<int>& unresolvedTokenIndex);
	void HandleVariableReference(TSNode node, ScopeStack& stack, std::vector<TSNode>& unresolvedEntry, std::vector<int>& unresolvedTokenIndex);
	void HandleNamedDecl(const TSNode nameNode, ScopeHandle currentScope, std::vector<std::tuple<Hash, TSNode>>& unresolvedTypes, std::vector<TSNode>& structs, bool exporting);
	void HandleUsingStatement(TSNode node, ScopeHandle scope, std::vector<std::tuple<Hash, TSNode>>& unresolvedTypes, std::vector<TSNode>& structs, bool& exporting);
	void FindDeclarations(TSNode scopeNode, ScopeHandle scope, ScopeStack& stack, bool& exporting, bool rebuild = false);
	void HandleFunctionDefnitionParameters(TSNode node, ScopeHandle currentScope, std::vector<std::tuple<Hash, TSNode>>& unresolvedNodes, Cursor& cursor);
	TypeHandle HandleFuncDefinitionNode(TSNode node, std::vector<TSNode>& structs, DeclarationFlags& flags);
	void CreateTopLevelScope(TSNode node, ScopeStack& stack, bool& exporting);
	void Build();
	void DoTokens(TSNode root, TSInputEdit* edits, int editCount);
	const std::optional<TypeHandle> EvaluateNodeExpressionType(TSNode node, const GapBuffer* buffer, ScopeHandle current, ScopeStack& stack);
	void RebuildScope(TSNode newScopeNode, TSInputEdit* edits, int editCount, TSNode root);



	/*
	int SearchScopeOffsets(int offset, const TSInputEdit& edit)
	{

		auto comp = [=](int a, int b) {

			auto edited = EditStartByte(a, edit.start_byte, edit.new_end_byte, edit.old_end_byte);
			return edited < b;
		};

		auto it = std::lower_bound(scopeOffsets.begin(), scopeOffsets.end(), offset, comp);

		if (it == scopeOffsets.end() || *it != offset)
		{
			return -1;
		}
		else
		{
			int index = std::distance(scopeOffsets.begin(), it);
			return index;
		}
	}
	*/



};
