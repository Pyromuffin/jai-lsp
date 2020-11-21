#include "Scope.h"


void Scope::Clear()
{
	size = 0;
	declarations.clear();
}

std::optional<ScopeDeclaration> Scope::TryGet(const Hash hash) const
{
#if SMALL
	if (size <= small_size)
	{
		for (int i = 0; i < size; i++)
		{
			if (small_hashes[i] == hash)
				return small_declarations[i];
		}

		return std::nullopt;
	}
#endif

	auto it = declarations.find(hash);
	if (it == declarations.end())
		return std::nullopt;

	return std::optional<ScopeDeclaration>(it->second);
}

void Scope::Add(const Hash hash, const ScopeDeclaration decl)
{
#if SMALL
	if (size < small_size)
	{
		small_hashes[size] = hash;
		small_declarations[size] = decl;
		size++;

		return;
	}
	if (size == small_size)
	{
		for (int i = 0; i < small_size; i++)
		{
			auto small_hash = small_hashes[i];
			auto small_decl = small_declarations[i];
			declarations.insert({ small_hash, small_decl });
		}
	}
#endif

	declarations.insert({ hash, decl });
	size++;
}

void Scope::AppendMembers(std::string& str, const GapBuffer* buffer, uint32_t upTo) const
{
#if SMALL
	if (size <= small_size)
	{
		for (int i = 0; i < size; i++)
		{
			auto decl = small_declarations[i];
			if (imperative && (int)decl.startByte > upTo)
				continue;

			for (int i = 0; i < decl.GetLength(); i++)
				str.push_back(buffer->GetChar(decl.startByte + i));

			str.push_back(',');
		}

		return;
	}
#endif

	for (auto& kvp : declarations)
	{
		if (kvp.second.flags & DeclarationFlags::BuiltIn)
			continue;

		if (imperative && (int)kvp.second.startByte > upTo)
			continue;

		for (int i = 0; i < kvp.second.GetLength(); i++)
			str.push_back(buffer->GetChar(kvp.second.startByte + i));

		str.push_back(',');
	}
}

void Scope::AppendExportedMembers(std::string& str, const GapBuffer* buffer)
{
#if SMALL
	if (size <= small_size)
	{
		for (int i = 0; i < size; i++)
		{
			auto decl = small_declarations[i];
			if (decl.flags & DeclarationFlags::Exported)
			{
				for (int i = 0; i < decl.GetLength(); i++)
					str.push_back(buffer->GetChar(decl.startByte + i));

				str.push_back(',');
			}
		}

		return;
	}
#endif
	for (auto& kvp : declarations)
	{
		if (kvp.second.flags & DeclarationFlags::Exported)
		{
			for (int i = 0; i < kvp.second.GetLength(); i++)
				str.push_back(buffer->GetChar(kvp.second.startByte + i));

			str.push_back(',');
		}
	}
}

void Scope::UpdateType(const Hash hash, const TypeHandle type)
{
#if SMALL
	if (size <= small_size)
	{
		for (int i = 0; i < size; i++)
		{
			if (small_hashes[i] == hash)
			{
				small_declarations[i].type = type;
				return;
			}
		}
	}
#endif

	declarations[hash].type = type;
}

void Scope::InjectMembersTo(Scope* otherScope)
{
#if SMALL
	if (size <= small_size)
	{
		for (int i = 0; i < size; i++)
		{
			auto decl = small_declarations[i];
			auto hash = small_hashes[i];
			otherScope->Add(hash, decl);
		}

		return;
	}
#endif
	for (auto& kvp : declarations)
	{
		otherScope->Add(kvp.first, kvp.second);
	}
}

uint16_t ScopeDeclaration::GetLength() const
{
	uint16_t length = this->length;
	length += (fourForEach & 0xF0);
	return length;
}

uint16_t ScopeDeclaration::GetRHSOffset() const
{
	uint16_t rhsOffset = this->rhsOffset;
	rhsOffset += (fourForEach << 4);
	return rhsOffset;
}

void ScopeDeclaration::SetLength(uint16_t length)
{
	assert(length < 4096);
	this->length = (length & 0xFF);
	fourForEach &= 0X0F;
	length &= 0X0F;
	fourForEach |= (length >> 4);
}

void ScopeDeclaration::SetRHSOffset(uint16_t rhsOffset)
{
	assert(rhsOffset < 4096);
	this->rhsOffset = (rhsOffset & 0xFF);
	fourForEach &= 0XF0;
	fourForEach |= (rhsOffset >> 8);
}
