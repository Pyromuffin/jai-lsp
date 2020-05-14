#pragma once
#include <chrono>
#include <iostream>
using namespace std::chrono;

class Timer
{
private:
	system_clock::time_point start;
	const char* name;
public:
	Timer(const char* name)
	{
		start = system_clock::now();
		this->name = name;
	}

	long long GetMicroseconds()
	{
		auto end = system_clock::now();
		auto duration = end - start;
		return duration_cast<microseconds>(duration).count();
	}

	void LogTimer()
	{
		std::cout << name << " duration: " << GetMicroseconds() << "\n";
	}
};

