#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <cassert>

#include "Hash.h"
#include "GapBuffer.h"

#define SMALL true


enum DeclarationFlags : uint8_t
{
	None = 0,
	Exported = 1 << 0,
	Constant = 1 << 1,
	Struct = 1 << 2,
	Enum = 1 << 3,
	Function = 1 << 4,
	BuiltIn = 1 << 5,
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
};


struct alignas(64) Scope
{
	static constexpr int small_size = 8;

public:
#if SMALL
	Hash small_hashes[small_size];
	ScopeDeclaration small_declarations[small_size];
#endif
	std::unordered_map<Hash, ScopeDeclaration> declarations;
	int size;

	TypeHandle associatedType = TypeHandle::Null();
	bool imperative;
	ScopeHandle parent;

	void Clear();
	std::optional<ScopeDeclaration> TryGet(const Hash hash) const;

	void Add(const Hash hash, const ScopeDeclaration decl);
	void AppendMembers(std::string& str, const GapBuffer* buffer, uint32_t upTo = UINT_MAX) const;
	void AppendExportedMembers(std::string& str, const GapBuffer* buffer);
	void UpdateType(const Hash hash, const TypeHandle type);
	void InjectMembersTo(Scope* otherScope);

};