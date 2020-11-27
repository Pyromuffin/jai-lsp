#include "Hashmap.h"
#include <cassert>
#include "stb_ds.h"

void Hashmap::Add(int key, ScopeHandle value)
{
#if _DEBUG
	for (int i = 0; i < Size(); i++)
	{
		assert(map[i].value.index != value.index);
	}
#endif
	hmput(map, key, value);
}

size_t Hashmap::GetIndex(int key)
{
	return hmgeti(map, key);
}

ScopeHandle Hashmap::Get(int key)
{
	assert(Contains(key));
	return hmget(map, key);
}

size_t Hashmap::Size()
{
	return hmlenu(map);
}

bool Hashmap::Remove(int key)
{
	return hmdel(map, key);
}

bool Hashmap::Contains(int key)
{
	return hmgeti(map, key) >= 0;
}

void Hashmap::Clear()
{
	hmfree(map);
}

Hashmap::kvp* Hashmap::Data()
{
	return map;
}

ScopeHandle Hashmap::operator[](size_t index)
{
	return map[index].value;
}





void Scopemap::Add(Hash key, ScopeDeclaration value)
{
	/*
#if _DEBUG
	for (int i = 0; i < Size(); i++)
	{
		assert(map[i].value.index != value.index);
	}
#endif
	*/
	idput(map, key, value);
}

int Scopemap::GetIndex(Hash key)
{
	ptrdiff_t temp;
	return (int)idgeti_ts(map, key, temp);
}

ScopeDeclaration Scopemap::Get(Hash key)
{
	assert(Contains(key));
	ptrdiff_t temp;
	return idget_ts(map, key, temp);
}

size_t Scopemap::Size() const 
{
	if (map == nullptr)
		return 0;

	return idlenu(map);
}

bool Scopemap::Remove(Hash key)
{
	return iddel(map, key);
}

bool Scopemap::Contains(Hash key)
{
	ptrdiff_t temp;
	return idgeti_ts(map, key, temp) >= 0;
}

void Scopemap::Clear()
{
	idfree(map);
}

Scopemap::kvp* Scopemap::Data()
{
	return map;
}

ScopeDeclaration Scopemap::operator[](size_t index) const
{
	return map[index].value;
}

void Scopemap::Update(size_t index, ScopeDeclaration value)
{
	map[index].value = value;
}















