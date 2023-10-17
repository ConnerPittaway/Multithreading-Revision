#include <iostream>
#include <vector>
#include <array>
#include <random>
#include <ranges>
#include <limits>
#include <cmath>
#include <thread>
#include <mutex>

#include "Timer.h"

constexpr size_t DATASET_SIZE = 50'000'000;

void ProcessData(std::array<int, DATASET_SIZE>& set, int &sum)
{
	for (int x : set)
	{
		constexpr auto limit = (double)std::numeric_limits<int>::max();
		const auto y = (double)x / limit;
		sum += int(std::sin(std::cos(y)) * limit);
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

	struct Value
	{
		int v = 0;
		char padding[60]; //Padding to fit on new cache line to avoid syncronisation
	};
	Value sum[4] = {0, 0, 0, 0};
	
	std::mutex mtx;
	timer.Mark();
	//Create threads
	for (size_t i = 0; i < 4; i++)
	{
		workers.push_back(std::thread{ ProcessData, std::ref(datasets[i]), std::ref(sum[i].v)});
	}

	//Join threads back to main
	for (auto& w : workers)
	{
		w.join();
	}
	auto t = timer.Peek();

	std::cout << "Result is " << sum[0] + sum[1] + sum[2] + sum[3] << std::endl;
	std::cout << "Processing the datasets took " << t << " seconds\n";
	return 0;
}