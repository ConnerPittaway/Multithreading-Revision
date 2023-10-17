#include <iostream>
#include <vector>
#include <array>
#include <random>
#include <ranges>
#include <limits>
#include <cmath>
#include <thread>
#include <mutex>
#include <span>

#include "Timer.h"

constexpr size_t DATASET_SIZE = 50'000'000;

void ProcessData(std::span<int> set, int &sum)
{
	for (int x : set)
	{
		constexpr auto limit = (double)std::numeric_limits<int>::max();
		const auto y = (double)x / limit;
		sum += int(std::sin(std::cos(y)) * limit);
	}
}

std::vector<std::array<int, DATASET_SIZE>> GenerateData()
{
	std::minstd_rand rne;
	std::vector<std::array<int, DATASET_SIZE>> datasets{ 4 };

	//Generate Random Numbers
	for (auto& arr : datasets)
	{
		std::ranges::generate(arr, rne);
	}

	return datasets;
}

int Big()
{
	auto datasets = GenerateData();

	std::vector<std::thread> workers;
	Timer timer;

	struct Value
	{
		int v = 0;
		char padding[60]; //Padding to fit on new cache line to avoid syncronisation
	};
	Value sum[4] = {0, 0, 0, 0};
	
	timer.Mark();
	//Create threads
	for (size_t i = 0; i < 4; i++)
	{
		workers.push_back(std::thread{ ProcessData, std::span(datasets[i]), std::ref(sum[i].v)});
	}

	//Join threads back to main
	for (auto& w : workers)
	{
		w.join();
	}
	auto t = timer.Peek();

	std::cout << "Result is " << sum[0].v + sum[1].v + sum[2].v + sum[3].v << std::endl;
	std::cout << "Processing the datasets took " << t << " seconds\n";
	return 0;
}

int Small()
{
	auto datasets = GenerateData();

	struct Value
	{
		int v = 0;
		char padding[60]; //Padding to fit on new cache line to avoid syncronisation
	};
	Value sum[4];

	Timer timer;
	timer.Mark();

	int total = 0;
	std::vector<std::jthread> workers;
	const auto subsetSize = DATASET_SIZE / 10'000;
	for (size_t i = 0; i < DATASET_SIZE; i += subsetSize)
	{
		for (size_t j = 0; j < 4; j++)
		{
			workers.push_back(std::jthread{ ProcessData, std::span(&datasets[j][i], subsetSize), std::ref(sum[j].v)});
		}
		workers.clear();
		total = 0;
	}
	auto t = timer.Peek();

	std::cout << "Result is " << total << std::endl;
	std::cout << "Processing the datasets took " << t << " seconds\n";

	return 0;
}

int main(int argc, char** argv)
{
	return Small();
}