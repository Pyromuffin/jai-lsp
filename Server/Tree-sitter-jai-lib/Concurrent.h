#pragma once
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <optional>

#include "Hash.h"


template <typename T>
struct ConcurrentDictionary
{
	std::shared_mutex mutex;
	std::unordered_map<Hash, T> dict;

	std::optional<T> Read(Hash key)
	{
		mutex.lock_shared();
		auto it = dict.find(key);

		if (it == dict.end())
		{
			mutex.unlock_shared();
			return std::nullopt;
		}

		auto value = it->second;
		mutex.unlock_shared();
		return std::optional(value);
	}

	void Write(Hash key, T value)
	{
		mutex.lock();
		dict[key] = value;
		mutex.unlock();
	}

	size_t size()
	{
		mutex.lock();
		auto size = dict.size();
		mutex.unlock();

		return size;
	}
};

template <typename T>
struct ConcurrentVector
{
	std::shared_mutex mutex;
	std::vector<T> vector;

	T Read(size_t index)
	{
		mutex.lock_shared();
		auto value = vector[index];
		mutex.unlock_shared();
		return value;
	}

	void Write(size_t index, T value)
	{
		mutex.lock();
		vector[index] = value;
		mutex.unlock();
	}

	void Append(T value)
	{
		mutex.lock();
		vector.push_back(value);
		mutex.unlock();
	}

	size_t size()
	{
		mutex.lock_shared();
		auto size = vector.size();
		mutex.unlock_shared();

		return size;
	}
};