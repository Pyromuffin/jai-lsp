export module Hashmap;

#include "stb_ds.h"

template<typename TK, typename TV>
class Hashmap
{
	struct kvp
	{
		TK key;
		TV value;
	};

	kvp* map = nullptr;

public:
	void Add(TK key, TV value)
	{
		hmput(map, key, value);
	}

	TV* GetPtr(TK key)
	{
		return hmgetp_null(map, key);
	}

	TV Get(TK key)
	{
		return hmget(map, key);
	}

	size_t Size()
	{
		return hmlenu(map);
	}

	bool Remove(TK key)
	{
		hmdel(map, key);
	}

	bool Contains(TK key)
	{
		return hmgeti(map, key) >= 0;
	}

};
