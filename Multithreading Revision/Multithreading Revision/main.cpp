#include <format>
#include <functional>
#include <condition_variable>
#include <sstream>
#include <deque>
#include <algorithm>

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
		ThreadPool(size_t numWorkers)
		{
			workers.reserve(numWorkers);
			for (size_t i = 0; i < numWorkers; i++)
			{
				workers.emplace_back(this);
			}
		}

		void Run(Task task)
		{
			{
				std::lock_guard lk{ taskQueueMtx_ };
				tasks_.push_back(std::move(task));
			}
			taskQueueCV_.notify_one();
		}

		void WaitForAllDone()
		{
			std::unique_lock lk{ taskQueueMtx_ };
			allDoneCV_.wait(lk, [this] {return tasks_.empty(); });
		}

		~ThreadPool()
		{
			for (auto& w : workers)
			{
				w.RequestStop();
			}
		}

	private:
		Task GetTask(std::stop_token& st)
		{
			Task task;
			std::unique_lock lk{ taskQueueMtx_ };
			taskQueueCV_.wait(lk, st, [this] {return !tasks_.empty(); });
			if (!st.stop_requested())
			{
				task = std::move(tasks_.front());
				tasks_.pop_front();
				if (tasks_.empty())
				{
					allDoneCV_.notify_all();
				}
			}
			return task;
		}

		class Worker
		{
		public:
			Worker(ThreadPool* tp) : thread_(std::bind_front(&Worker::RunKernel, this)), pool_{tp} {}
			void RequestStop()
			{
				thread_.request_stop();
			}
		private:
			//Functions
			void RunKernel(std::stop_token st)
			{
				while(auto task = pool_->GetTask(st))
				{
					task();
				}
			}

			//Data
			ThreadPool* pool_;
			std::jthread thread_;
		};
		std::mutex taskQueueMtx_;
		std::condition_variable_any taskQueueCV_;
		std::condition_variable_any allDoneCV_;
		std::deque<Task> tasks_;
		std::vector<Worker> workers;
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

	using namespace std::chrono_literals;
	const auto spit = []
	{

		std::ostringstream ss;
		ss << std::this_thread::get_id();
		std::cout << std::format("<< {} >>\n", ss.str());
	};

	tk::ThreadPool pool{ 4 };

	return 0;
}