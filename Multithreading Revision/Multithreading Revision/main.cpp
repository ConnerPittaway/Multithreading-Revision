#include <iostream>
#include <vector>
#include <array>
#include <random>
#include <thread>
#include <mutex>
#include <span>
#include <algorithm>
#include <numeric>
#include <numbers>
#include <fstream>
#include <format>
#include <functional>

#include "Timer.h"
#include "Constants.h"
#include "Task.h"
#include "Preassigned.h"
#include "Timing.h"
#include "Queued.h"
#include "popl.h"

int main(int argc, char** argv)
{
	using namespace popl;

	OptionParser op("Allowed options");
	auto stacked = op.add<Switch>("", "stacked", "Generate a stacked dataset");
	auto even = op.add<Switch>("", "even", "Generate an even dataset");
	auto queued = op.add<Switch>("", "queued", "Use task queue");
	op.parse(argc, argv);

	Dataset data;
	if (stacked->is_set())
	{
		data = GenerateDataStacked();
	}
	else if (even->is_set())
	{
		data = GenerateDataEvenly();
	}
	else
	{
		data = GenerateDataRandom();
	}

	if (queued->is_set())
	{
		return que::Experiment(std::move(data));
	}
	else
	{
		return pre::Experiment(std::move(data));
	}
}