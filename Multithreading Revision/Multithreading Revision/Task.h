#pragma once
#include <random>
#include <array>
#include <ranges>
#include <cmath>
#include <numbers>

#include "Constants.h"

struct Task
{
	double val;
	bool heavy;
	unsigned int Process() const
	{
		const auto iterations = heavy ? HeavyIterations : LightIterations;
		auto intermediate = val;
		for (size_t i = 0; i < iterations; i++)
		{
			const auto digits = unsigned int(std::abs(std::sin(std::cos(intermediate) * std::numbers::pi) * 10'000'000.)) % 100'000;
			intermediate = double(digits) / 10'000.; //Value between 0 and 10;
		}
		return unsigned int(std::exp(intermediate));
	};
};

using Dataset = std::vector<std::array<Task, ChunkSize>>;

Dataset GenerateDataRandom()
{
	std::minstd_rand rne; //Random Number Engine 
	std::bernoulli_distribution hDist{ ProbabilityHeavy }; //Heavy Distribution
	std::uniform_real_distribution rDist{ 0., 2. * std::numbers::pi };

	std::vector<std::array<Task, ChunkSize>> chunks(ChunkCount);

	for (auto& chunk : chunks)
	{
		std::ranges::generate(chunk, [&] {return Task{ .val = rDist(rne), .heavy = hDist(rne) }; });
	}

	return chunks;
}

Dataset GenerateDataEvenly()
{
	std::minstd_rand rne; //Random Number Engine 

	std::uniform_real_distribution rDist{ 0., 2. * std::numbers::pi };

	const int everyNth = int(1. / ProbabilityHeavy);

	std::vector<std::array<Task, ChunkSize>> chunks(ChunkCount);

	for (auto& chunk : chunks)
	{
		std::ranges::generate(chunk, [&, i = 0.]() mutable {
			bool heavy = false;
			if ((i += ProbabilityHeavy) >= 1.)
			{
				i -= 1.;
				heavy = true;
			}
			return Task{ .val = rDist(rne), .heavy = heavy };
			});
	}

	return chunks;
}

Dataset GenerateDataStacked()
{
	auto data = GenerateDataEvenly();

	for (auto& chunk : data)
	{
		std::ranges::partition(chunk, std::identity{}, &Task::heavy);
	}


	return data;
}