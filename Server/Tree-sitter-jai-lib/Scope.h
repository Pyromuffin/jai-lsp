#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <cassert>

#include "Hash.h"
#include "GapBuffer.h"


enum DeclarationFlags : uint8_t
{
	None = 0,
	TypeFlag0 = 1 << 0,
	TypeFlag1 = 1 << 1, 

	Struct = TypeFlag0,
	Enum = TypeFlag0 | TypeFlag1,
	Function = TypeFlag1,

	Exported = 1 << 3,
	Expression =  1 << 4,
	Constant = 1 << 5,
	Using = 1 << 6,
	Evaluated = 1 << 7,
};

struct ScopeHandle
{
	uint16_t index;
};

struct TypeKing
{
	enum Kind
	{
		structure,
		function
	};

	std::string name;
	std::vector<std::string> parameters;
	ScopeHandle members;
};

struct TypeHandle
{
	uint16_t fileIndex;
	uint16_t index;

	static constexpr TypeHandle Null()
	{
		return TypeHandle{ .fileIndex = UINT16_MAX, .index = UINT16_MAX };
	}

	bool operator==(const TypeHandle& rhs) const
	{
		return fileIndex == rhs.fileIndex && index == rhs.index;
	}

};

inline DeclarationFlags operator|(DeclarationFlags a, DeclarationFlags b)
{
	return static_cast<DeclarationFlags>(static_cast<int>(a) | static_cast<int>(b));
}


struct ScopeDeclaration
{
	// we need to find somewhere to put the RHS offset from the start byte.

	uint32_t startByte;
	DeclarationFlags flags;

private:
	uint8_t length;
	uint8_t rhsOffset;
	uint8_t fourForEach;

public:
	union {
		TypeHandle type;
		const void* id;
	};

	uint16_t GetLength() const;
	uint16_t GetRHSOffset() const;
	void SetLength(uint16_t length);
	void SetRHSOffset(uint16_t rhsOffset);
	bool HasFlags(DeclarationFlags flags)
	{
		return (this->flags & flags) == flags;
	}
};







class Scopemap
{
	struct kvp
	{
		Hash key;
		ScopeDeclaration value;
	};

	kvp* map = nullptr;

public:
	void Add(Hash key, ScopeDeclaration value);
	int GetIndex(Hash key);
	void Update(size_t index, ScopeDeclaration value);
	ScopeDeclaration Get(Hash key);
	size_t Size() const;
	bool Remove(Hash key);
	bool Contains(Hash key);
	void Clear();
	kvp* Data();
	ScopeDeclaration operator[](size_t) const;
};



struct Scope
{
	Scopemap declarations;
	TypeHandle associatedType = TypeHandle::Null();
	bool imperative;
	ScopeHandle parent;
	std::vector<ScopeDeclaration> usings;
	
	void Clear();
	std::optional<ScopeDeclaration> TryGet(const Hash hash);
	void Add(const Hash hash, const ScopeDeclaration decl);
	void AppendMembers(std::string& str, const GapBuffer* buffer, uint32_t upTo = UINT_MAX);
	void AppendExportedMembers(std::string& str, const GapBuffer* buffer);
	void UpdateDeclaration(const size_t index, const ScopeDeclaration type);
	void InjectMembersTo(Scope* otherScope, uint32_t atPosition);
	ScopeDeclaration* GetDeclFromIndex(int index);
	int GetIndex(const Hash hash);
};

constexpr auto scopeSize = sizeof(Scope); // i think these are cacheline aligned so we can spend 64 bytes on whatever we want !