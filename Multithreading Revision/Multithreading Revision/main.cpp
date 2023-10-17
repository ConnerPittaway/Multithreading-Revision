#include <iostream>
#include <vector>
#include <array>
#include <random>
#include <thread>
#include <mutex>
#include <span>
#include <algorithm>
#include <numeric>

#include "Timer.h"

constexpr size_t WorkerCount = 4;
constexpr size_t ChunkSize = 1000;
constexpr size_t ChunkCount = 100;
constexpr size_t SubsetSize = ChunkSize / WorkerCount;
constexpr size_t LightIterations = 100;
constexpr size_t HeavyIterations = 1'000;

constexpr double ProbabilityHeavy = .02;

static_assert(ChunkSize >= WorkerCount);
static_assert(ChunkSize% WorkerCount == 0);

struct Task
{
	unsigned int val;
	bool heavy;
	unsigned int Process() const
	{
		const auto iterations = heavy ? HeavyIterations : LightIterations;
		double intermediate = 2. * (double(val) / double(std::numeric_limits<unsigned int>::max())) - 1.;
		for (size_t i = 0; i < iterations; i++)
		{
			intermediate = std::sin(std::cos(intermediate));
		}
		return unsigned int((intermediate + 1.) / 2. * double(std::numeric_limits<unsigned int>::max()));
	};
};

std::vector<std::array<Task, ChunkSize>> GenerateData()
{
	std::minstd_rand rne; //Random Number Engine 
	std::bernoulli_distribution hDist{ ProbabilityHeavy }; //Heavy Distribution

	std::vector<std::array<Task, ChunkSize>> chunks(ChunkCount);
	for (auto& chunk : chunks)
	{
		std::ranges::generate(chunk, [&] {return Task{ .val = rne(), .heavy = hDist(rne) }; });
	}

	return chunks;
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

private:
	void ProcessData_()
	{
		for (const auto& task : input)
		{
			accululation += task.Process();
		}
	}

	void Run_()
	{
		std::unique_lock lk{ mtx };
		while (true)
		{
			cv.wait(lk, [this] {return !input.empty() || terminate; });
			if (terminate)
			{
				break;
			}
			ProcessData_();
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
};

int Experiment()
{
	const auto chunks = GenerateData();

	Timer timer;
	timer.Mark();

	//Create Worker Threads
	WorkerController workerController; //Initialise Controller
	std::vector<std::unique_ptr<Worker>> workerPtrs;
	for (size_t i = 0; i < WorkerCount; i++)
	{
		workerPtrs.push_back(std::make_unique<Worker>(&workerController));
	}

	for (const auto& chunk : chunks)
	{
		for (size_t iSubset = 0; iSubset < WorkerCount; iSubset++)
		{
			workerPtrs[iSubset]->SetJob(std::span{ &chunk[iSubset * SubsetSize], SubsetSize });
			//workerThreads.push_back(std::jthread{ ProcessData, std::span{&datasets[j][i], subsetSize}, std::ref(sum[j].i)});
		}
		workerController.WaitForAllDone();
	}

	auto t = timer.Peek();
	std::cout << "Processing took " << t << " seconds\n";
	unsigned int result = 0;
	for (const auto& w : workerPtrs)
	{
		result += w->GetResult();
	}
	std::cout << "Result is " << result << std::endl;

	return 0;
}


/*struct Value
{
	int i = 0;
	char padding[60]; //Padding to avoid syncronisation
};
Value sum[4];*/

int DoBig()
{
	auto datasets = GenerateData();

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
	return Experiment();
}