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

namespace que
{
	class WorkerControllerQueued
	{
	public:
		WorkerControllerQueued() : lk{ mtx } {}
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

		void SetChunk(std::span<const Task> chunk)
		{
			idx = 0;
			currentChunk = chunk;
		}

		const Task* GetTask()
		{
			//std::lock_guard lock{ mtx };
			const auto i = idx.fetch_add(1);
			if (i >= ChunkSize)
			{
				return nullptr;
			}
			return &currentChunk[i];
		}

	private:
		std::condition_variable cv;
		std::mutex mtx; //Always Mutex with CV
		std::unique_lock<std::mutex> lk;
		std::span<const Task> currentChunk;
		//Shared Memory
		int doneCount = 0;
		std::atomic<size_t> idx = 0;
	};

	class WorkerQueued
	{
	public:
		WorkerQueued(WorkerControllerQueued* pWorkerController)
			:
			pController{ pWorkerController },
			thread{ &WorkerQueued::Run_, this }
		{}

		void StartWork()
		{
			{
				std::lock_guard lk{ mtx };
				working = true;
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

		~WorkerQueued()
		{
			Kill();
		}

	private:
		void ProcessData_()
		{
			numHeavyItems = 0;
			while (auto pTask = pController->GetTask())
			{
				accululation += pTask->Process();

				if constexpr (timingMeasurementEnabled)
				{
					numHeavyItems += pTask->heavy;
				}
			}
		}

		void Run_()
		{
			std::unique_lock lk{ mtx };
			while (true)
			{
				Timer timer;
				cv.wait(lk, [this] {return working || terminate; });
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

				working = false;
				pController->SignalDone();
			}
		}
		WorkerControllerQueued* pController;
		std::jthread thread;
		std::condition_variable cv;
		std::mutex mtx;

		//Shared Memory
		unsigned int accululation = 0;
		bool terminate = false;
		bool working = false;
		float workTime = -1.f;
		size_t numHeavyItems;
	};

	int Experiment(Dataset chunks)
	{
			Timer totalTime;
			totalTime.Mark();

			//Create Worker Threads
			WorkerControllerQueued workerController; //Initialise Controller
			std::vector<std::unique_ptr<WorkerQueued>> workerPtrs(WorkerCount);
			std::ranges::generate(workerPtrs, [&workerController] {return std::make_unique<WorkerQueued>(&workerController); });

			std::vector<ChunkTimeInfo> timings;
			timings.reserve(ChunkCount);

			Timer chunkTimer;
			for (const auto& chunk : chunks)
			{
				if constexpr (timingMeasurementEnabled)
				{
					chunkTimer.Mark();
				}

				workerController.SetChunk(chunk);
				for (const auto& w : workerPtrs)
				{
					w->StartWork();
					//workerPtrs[iSubset]->SetJob(std::span{ &chunk[iSubset * SubsetSize], SubsetSize });
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