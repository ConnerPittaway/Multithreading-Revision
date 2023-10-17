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

class WorkerController
{
public:
	WorkerController(int workerCount) : lk{ mtx }, workerCount{ workerCount } {}
	void SignalDone()
	{
		bool needsNotification = false;
		{
			std::lock_guard lk{ mtx };
			++doneCount;
			if (doneCount == workerCount)
			{
				cv.notify_one();
			}
		}
	}

	void WaitForAllDone()
	{
		cv.wait(lk, [this] {return doneCount == workerCount; });
		doneCount = 0;
	}

private:
	std::condition_variable cv;
	std::mutex mtx; //Always Mutex with CV
	std::unique_lock<std::mutex> lk;
	int workerCount = 0;

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

	void SetJob(std::span<int> data, int *pOut)
	{
		{
			std::lock_guard lk{ mtx };
			input = data;
			pOutput = pOut;
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

private:

	void Run_()
	{
		std::unique_lock lk{ mtx };
		while (true)
		{
			cv.wait(lk, [this] {return pOutput !=nullptr || terminate; });
			if (terminate)
			{
				break;
			}
			ProcessData(input, *pOutput);
			pOutput = nullptr;
			input = {};
			pController->SignalDone();
		}
	}

	WorkerController* pController;
	std::jthread thread;
	std::condition_variable cv;
	std::mutex mtx;

	//Shared Memory
	std::span<int> input;
	int* pOutput = nullptr;
	bool terminate = false;
};

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

	//Create Worker Threads
	constexpr size_t workerCount = 4;
	WorkerController workerController{ workerCount }; // Initialise Controller
	std::vector<std::unique_ptr<Worker>> workerPtrs;
	for (size_t i = 0; i < workerCount; i++)
	{
		workerPtrs.push_back(std::make_unique<Worker>(&workerController));
	}

	const auto subsetSize = DATASET_SIZE / 10'000;
	for (size_t i = 0; i < DATASET_SIZE; i += subsetSize)
	{
		for (size_t j = 0; j < 4; j++)
		{
			workerPtrs[j]->SetJob(std::span(&datasets[j][i], subsetSize), &sum[j].v);
			//workers.push_back(std::jthread{ ProcessData, std::span(&datasets[j][i], subsetSize), std::ref(sum[j].v)});
		}
		workerController.WaitForAllDone();
	}
	auto t = timer.Peek();

	std::cout << "Result is " << sum[0].v + sum[1].v + sum[2].v + sum[3].v << std::endl;
	std::cout << "Processing the datasets took " << t << " seconds\n";

	for (auto& w : workerPtrs)
	{
		w->Kill();
	}

	return 0;
}

int main(int argc, char** argv)
{
	return Small();
}