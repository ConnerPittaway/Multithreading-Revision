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

constexpr bool timingMeasurementEnabled = true;
constexpr size_t WorkerCount = 4;
constexpr size_t ChunkSize = 8'000;
constexpr size_t ChunkCount = 100;
constexpr size_t SubsetSize = ChunkSize / WorkerCount;
constexpr size_t LightIterations = 100;
constexpr size_t HeavyIterations = 1'000;

constexpr double ProbabilityHeavy = .05;

static_assert(ChunkSize >= WorkerCount);
static_assert(ChunkSize % WorkerCount == 0);

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

struct ChunkTimeInfo
{
	std::array<float, WorkerCount> timeSpentWorkingPerThread;
	std::array<size_t, WorkerCount> numberOfHeavyPerThread;
	float totalChunkTime;
};

std::vector<std::array<Task, ChunkSize>> GenerateDataRandom()
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

std::vector<std::array<Task, ChunkSize>> GenerateDataEvenly()
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

std::vector<std::array<Task, ChunkSize>> GenerateDataStacked()
{
	auto data = GenerateDataEvenly();

	for (auto& chunk : data)
	{
		std::ranges::partition(chunk, std::identity{}, &Task::heavy);
	}


	return data;
}

class WorkerController
{
public:
	WorkerController() : lk{ mtx } {}
	void SignalDone()
	{
		bool needsNotification = false;
		{
			std::lock_guard lk{ mtx };
			++doneCount;
			if (doneCount == WorkerCount)
			{
				needsNotification = true;

			}
		}

		if (needsNotification)
		{
			cv.notify_one();
		}
	}

	void WaitForAllDone()
	{
		cv.wait(lk, [this] {return doneCount == WorkerCount; });
		doneCount = 0;
	}

private:
	std::condition_variable cv;
	std::mutex mtx; //Always Mutex with CV
	std::unique_lock<std::mutex> lk;

	//Shared Memory
	int doneCount = 0;
};

class Worker
{
public:
	Worker(WorkerController* pWorkerController)
		:
		pController{ pWorkerController },
		thread{ &Worker::Run_, this }
	{}

	void SetJob(std::span<const Task> data)
	{
		{
			std::lock_guard lk{ mtx };
			input = data;
		}
		cv.notify_one();
	}

	void Kill()
	{
		{
			std::lock_guard lk{ mtx };
			terminate = true;
		}
		cv.notify_one();
	}

	unsigned int GetResult() const
	{
		return accululation;
	}

	float GetJobWorkTime() const
	{
		return workTime;
	}

	size_t GetNumHeavy() const
	{
		return numHeavyItems;
	}

	~Worker()
	{
		Kill();
	}

private:
	void ProcessData_()
	{
		numHeavyItems = 0;
		for (const auto& task : input)
		{
			accululation += task.Process();

			if constexpr (timingMeasurementEnabled)
			{
				numHeavyItems += task.heavy;
			}
		}
	}

	void Run_()
	{
		std::unique_lock lk{ mtx };
		while (true)
		{
			Timer timer;
			cv.wait(lk, [this] {return !input.empty() || terminate; });
			if (terminate)
			{
				break;
			}

			if constexpr (timingMeasurementEnabled)
			{
				timer.Mark();
			}
			ProcessData_();
			if constexpr (timingMeasurementEnabled)
			{
				workTime = timer.Peek();
			}

			input = {};
			pController->SignalDone();
		}
	}
	WorkerController* pController;
	std::jthread thread;
	std::condition_variable cv;
	std::mutex mtx;

	//Shared Memory
	std::span<const Task>input;
	unsigned int accululation = 0;
	bool terminate = false;
	float workTime = -1.f;
	size_t numHeavyItems;
};

int Experiment(bool stacked)
{
	const auto chunks = [=]
	{
		if (stacked)
		{
			return GenerateDataStacked();
		}
		else
		{
			return GenerateDataEvenly();
		}
	}();

	Timer totalTime;
	totalTime.Mark();

	//Create Worker Threads
	WorkerController workerController; //Initialise Controller
	std::vector<std::unique_ptr<Worker>> workerPtrs(WorkerCount);
	std::ranges::generate(workerPtrs, [&workerController] {return std::make_unique<Worker>(&workerController); });

	std::vector<ChunkTimeInfo> timings;
	timings.reserve(ChunkCount);

	Timer chunkTimer;
	for (const auto& chunk : chunks)
	{
		if constexpr (timingMeasurementEnabled)
		{
			chunkTimer.Mark();
		}
		for (size_t iSubset = 0; iSubset < WorkerCount; iSubset++)
		{
			workerPtrs[iSubset]->SetJob(std::span{ &chunk[iSubset * SubsetSize], SubsetSize });
			//workerThreads.push_back(std::jthread{ ProcessData, std::span{&datasets[j][i], subsetSize}, std::ref(sum[j].i)});
		}
		workerController.WaitForAllDone();

		const auto chunkTime = chunkTimer.Peek();

		if constexpr (timingMeasurementEnabled)
		{
			timings.push_back({});
			for (size_t i = 0; i < WorkerCount; i++)
			{
				timings.back().numberOfHeavyPerThread[i] = workerPtrs[i]->GetNumHeavy();
				timings.back().timeSpentWorkingPerThread[i] = workerPtrs[i]->GetJobWorkTime();
			}
			timings.back().totalChunkTime = chunkTime;
		}
	}

	auto t = totalTime.Peek();
	std::cout << "Processing took " << t << " seconds\n";

	unsigned int result = 0;
	for (const auto& w : workerPtrs)
	{
		result += w->GetResult();
	}
	std::cout << "Result is " << result << std::endl;

	if constexpr (timingMeasurementEnabled)
	{
		//Output CSV of timings
		// work-time, idle-time, number of heavies x workers + total time, total heavies
		std::ofstream csv{ "timings.csv", std::ios_base::trunc };
		for (size_t i = 0; i < WorkerCount; i++)
		{
			csv << std::format("work_{0:}, idle_{0:}, heavy_{0:}, ", i);
		}
		csv << "chunk_time, total_idle, total_heavy\n";

		for (const auto& chunk : timings)
		{
			float totalIdle = 0.f;
			size_t totalHeavy = 0;
			for (size_t i = 0; i < WorkerCount; i++)
			{
				const auto idle = chunk.totalChunkTime - chunk.timeSpentWorkingPerThread[i];
				const auto heavy = chunk.numberOfHeavyPerThread[i];
				csv << std::format("{},{},{},", chunk.timeSpentWorkingPerThread[i], idle, heavy);
				totalIdle += idle;
				totalHeavy += heavy;
			}
			csv << std::format("{},{},{}\n", chunk.totalChunkTime, totalIdle, totalHeavy);
		}
	}
	return 0;
}

int DoBig()
{
	auto datasets = GenerateDataRandom();

	std::vector<std::thread> workerThreads;
	Timer timer;

	struct Value
	{
		int i = 0;
		char padding[60]; //Padding to avoid syncronisation
	};
	Value sum[4];

	timer.Mark();
	for (size_t i = 0; i < 4; i++)
	{
		//workerThreads.push_back(std::thread{ ProcessData, std::span{datasets[i]}, std::ref(sum[i].i) });
	}

	for (auto& wt : workerThreads)
	{
		wt.join();
	}
	auto t = timer.Peek();

	std::cout << "Set generation took " << t << " seconds\n";
	std::cout << "Result is " << sum[0].i + sum[1].i + sum[2].i + sum[3].i << std::endl;
	return 0;
}

int main(int argc, char** argv)
{
	return Experiment(true);
}