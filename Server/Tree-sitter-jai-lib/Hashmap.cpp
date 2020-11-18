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