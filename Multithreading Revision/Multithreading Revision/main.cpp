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
#include <condition_variable>

#include "Timer.h"
#include "Constants.h"
#include "Task.h"
#include "Preassigned.h"
#include "Timing.h"
#include "Queued.h"
#include "AtomicQueue.h"
#include "popl.h"

namespace tk
{
	using Task = std::function<void()>;

	class ThreadPool
	{
	public:
		void Run(Task task)
		{
			if (auto i = std::ranges::find_if(workers,[](const auto& w) {return !w->isBusy();});
			i != workers.end())
			{
				(*i)->Run(std::move(task));
			}
			else
			{
				workers.push_back(std::make_unique<Worker>());
				workers.back()->Run(std::move(task));
			}
		};

		bool isRunningTasks()
		{
			return std::ranges::any_of(workers, [](const auto& w) {return w->isBusy(); });
		};

	private:

		class Worker
		{
		public:
			Worker() : thread_(&Worker::RunKernel, this){}
			bool isBusy() const
			{
				return busy_;
			}
			void Run(Task task)
			{
				task_ = std::move(task);
				busy_ = true;
				cv.notify_one();
			}
		private:
			//Functions
			void RunKernel()
			{
				std::unique_lock lk{ mtx };
				auto st = thread_.get_stop_token();
				while(cv.wait(lk, st, [this]() -> bool {return busy_; }))
				{
					task_();
					task_ = {};
					busy_ = false;
				}
			}

			//Data
			std::atomic<bool> busy_ = false;
			std::condition_variable_any cv;
			std::mutex mtx;
			Task task_;
			std::jthread thread_;
		};
		std::vector<std::unique_ptr<Worker>> workers;
	};
}

int main(int argc, char** argv)
{
	/*using namespace popl;

	OptionParser op("Allowed options");
	auto stacked = op.add<Switch>("", "stacked", "Generate a stacked dataset");
	auto even = op.add<Switch>("", "even", "Generate an even dataset");
	auto queued = op.add<Switch>("", "queued", "Use task queued approach");
	auto atomicQueued = op.add<Switch>("", "atomic-queued", "Use atomic queued approach");
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
	else if (atomicQueued->is_set())
	{
		return atq::Experiment(std::move(data));
	}
	else
	{
		return pre::Experiment(std::move(data));
	}*/

	tk::ThreadPool pool;
	pool.Run([] {std::cout << "Hel" << std::endl; });
	pool.Run([] {std::cout << "lo" << std::endl; });
	while (pool.isRunningTasks())
	{
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(16ms);
	}
	return 0;
}