#include "Scope.h"


void Scope::Clear()
{
	declarations.Clear();
}

std::optional<ScopeDeclaration> Scope::TryGet(const Hash hash)
{
	auto index = declarations.GetIndex(hash);

	if (index >= 0)
	{
		return declarations[index];
	}

	return std::nullopt;
}


void Scope::Add(const Hash hash, const ScopeDeclaration decl)
{
	declarations.Add(hash, decl);
}

void Scope::AppendMembers(std::string& str, const GapBuffer* buffer, uint32_t upTo)
{
	auto size = declarations.Size();
	auto data = declarations.Data();

	for (int i = 0; i < size; i++)
	{
		auto& decl = data[i].value;
		if (decl.flags & DeclarationFlags::BuiltIn)
			continue;

		if (imperative && (int)decl.startByte > upTo)
			continue;

		for (int i = 0; i < decl.GetLength(); i++)
			str.push_back(buffer->GetChar(decl.startByte + i));

		str.push_back(',');
	}
}

void Scope::AppendExportedMembers(std::string& str, const GapBuffer* buffer)
{
	auto size = declarations.Size();
	auto data = declarations.Data();

	for (int i = 0; i < size; i++)
	{
		auto& decl = data[i].value;
		if (decl.flags & DeclarationFlags::Exported)
		{
			for (int i = 0; i < decl.GetLength(); i++)
				str.push_back(buffer->GetChar(decl.startByte + i));

			str.push_back(',');
		}
	}
}

void Scope::UpdateDeclaration(const size_t index, const ScopeDeclaration decl)
{
	declarations.Update(index, decl);
}

void Scope::InjectMembersTo(Scope* otherScope, uint32_t atPosition)
{
	auto size = declarations.Size();
	auto data = declarations.Data();

	for (int i = 0; i < size; i++)
	{
		auto decl = data[i].value;
		//decl.startByte = atPosition;
		otherScope->Add(data[i].key, decl);
	}
}

ScopeDeclaration* Scope::GetDeclFromIndex(int index)
{
	auto data = declarations.Data();
	return &data[index].value;
}

int Scope::GetIndex(const Hash hash)
{
	return declarations.GetIndex(hash);
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
