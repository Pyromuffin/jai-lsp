#ifndef __INTELLISENSE__ 
module;
#include "stb_ds.h"
#endif

export module HashmapModule;


export template<typename TK, typename TV>
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

	size_t GetIndex(TK key)
	{
		return hmgeti(map, key);
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
		return hmdel(map, key);
	}

	bool Contains(TK key)
	{
		return hmgeti(map, key) >= 0;
	}

	void Clear()
	{
		hmfree(map);
	}

	kvp* Data()
	{
		return map;
	}

	TV operator[](size_t);



};

template<typename TK, typename TV>
 TV Hashmap<TK,TV>::operator[](size_t index)
{
	 return map[index].value;
}