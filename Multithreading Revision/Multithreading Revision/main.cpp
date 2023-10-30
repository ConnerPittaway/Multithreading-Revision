#include <format>
#include <functional>
#include <condition_variable>
#include <sstream>
#include <deque>
#include <algorithm>
#include <optional>
#include <semaphore>
#include <thread>
#include <assert.h>

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
	template<typename T>
	class SharedState
	{
	public:
		template<typename R>
		void Set(R&& result)
		{
			if (!result_)
			{
				result_ = std::forward<R>(result);
				readySignal_.release();
			}
		}

		T Get()
		{
			readySignal_.acquire();
			return std::move(*result_);
		}
	private:
		std::binary_semaphore readySignal_{ 0 }; //Similar to mutex but not tied to thread of execution, acquired on one thread and released on another
		std::optional<T> result_;
	};

	template<typename T>
	class Promise;

	template<typename T>
	class Future
	{
		friend class Promise<T>;
	public:
		T Get()
		{
			assert(!resultAcquired);
			resultAcquired = true;
			return pState_->Get();
		}
	private:
		Future(std::shared_ptr<SharedState<T>> pState) : pState_{pState}{} //Private so promise (friend class) can only construct futures
		bool resultAcquired = false;
		std::shared_ptr<SharedState<T>> pState_;
	};

	template<typename T>
	class Promise
	{
	public:
		Promise() : pState_{ std::make_shared<SharedState<T>>() } {}

		template<typename R>
		void Set(R&& result)
		{
			pState_->Set(std::forward<R>(result));
		}

		Future<T> GetFuture()
		{
			assert(futureAvailable);
			futureAvailable = false;
			return { pState_ }; //Return future constructed with pState_
		}
	private:
		bool futureAvailable = true;
		std::shared_ptr<SharedState<T>> pState_;
	};

	class Task
	{
	public:
		Task() = default;
		Task(const Task&) = delete;
		Task(Task&& donor) noexcept : executor_{std::move(donor.executor_)} {}
		Task& operator = (const Task&) = delete;
		Task& operator = (Task&& rhs) noexcept
		{
			executor_ = std::move(rhs.executor_);
			return *this;
		}

		void operator()()
		{
			executor_();
		}

		operator bool() const
		{
			return(bool)executor_;
		}

		template<typename F, typename ...A>
		static auto Make(F&& function, A&& ...args)
		{
			Promise<std::invoke_result_t<F, A...>> promise;
			auto future = promise.GetFuture();
			return std::make_pair(
				Task{ std::forward<F>(function), std::move(promise), std::forward<A>(args)... },
				std::move(future)
			);
		}
	private:
		//Fun
		template<typename F, typename P, typename...A>
		Task(F&& function, P&& promise, A&&...args)
		{
			executor_ = [
			function = std::forward<F>(function),
			promise = std::forward<P>(promise),
			...args = std::forward<A>(args)
			]() mutable
			{
				promise.Set(function(std::forward<A>(args)...));
			};
		}
		//Var
		std::function<void()> executor_;
	};

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
	/*
	const auto spit = []
	{

		std::ostringstream ss;
		ss << std::this_thread::get_id();
		std::cout << std::format("<< {} >>\n", ss.str());
	};

	tk::ThreadPool pool{ 4 };*/

	tk::Promise<int> prom;
	auto fut = prom.GetFuture();

	std::thread{ [](tk::Promise<int> p)
		{
			std::this_thread::sleep_for(2'500ms);
			p.Set(69);
		}, std::move(prom)
	}.detach();

	std::cout << fut.Get();

	auto [task, future] = tk::Task::Make([](int x)
		{
			std::this_thread::sleep_for(1'500ms);
			return x + 42000;
		}, 69);

	std::thread{ std::move(task) }.detach();
	std::cout << future.Get();

	return 0;
}