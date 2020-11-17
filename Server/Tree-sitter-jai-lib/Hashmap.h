#pragma once
#include "TreeSitterJai.h"

class Hashmap
{
	struct kvp
	{
		int key;
		ScopeHandle value;
	};

	kvp* map = nullptr;

public:
	void Add(int key, ScopeHandle value);
	size_t GetIndex(int key);
	ScopeHandle Get(int key);
	size_t Size();
	bool Remove(int key);
	bool Contains(int key);
	void Clear();
	kvp* Data();
	ScopeHandle operator[](size_t);
};


