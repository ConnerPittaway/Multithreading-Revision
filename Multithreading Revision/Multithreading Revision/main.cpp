#include <iostream>
#include <vector>
#include <array>
#include <random>
#include <ranges>
#include "Timer.h"

constexpr size_t DATASET_SIZE = 50'000'000;

void ProcessData(std::array<int, DATASET_SIZE>& set)
{
	for (int x : set)
	{
		constexpr auto limit = (double)std::numeric_limits<int>::max();
		const auto y = (double)x / limit;
		set[0] += int(std::sin(std::cos(y)) * limit);
	}
}

int main()
{
	std::minstd_rand rne;
	std::vector<std::array<int, DATASET_SIZE>> datasets{ 4 };
	std::vector<std::thread> workers;
	Timer timer;

	//Generate Random Numbers
	for (auto& arr : datasets)
	{
		std::ranges::generate(arr, rne);
	}

	timer.Mark();

	//Create threads
	for (auto& set : datasets)
	{
		workers.push_back(std::thread{ ProcessData, std::ref(set) });
	}

	//Join threads back to main
	for (auto& w : workers)
	{
		w.join();
	}
	auto t = timer.Peek();

	std::cout << "Processing the datasets took " << t << " seconds\n";
	return 0;
}