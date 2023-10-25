#pragma once
#include <iostream>
#include <thread>
#include <mutex>
#include <span>
#include <format>

#include "Constants.h"
#include "Task.h"
#include "Timing.h"
#include "Timer.h"

namespace pre
{
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

	int Experiment(Dataset chunks)
	{
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
				WriteCSV(timings);
			}
			return 0;
	}
}