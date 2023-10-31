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
#include <ranges>
#include <variant>
#include <future>

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
			std::promise<std::invoke_result_t<F, A...>> promise;
			auto future = promise.get_future();
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
				try {
					if constexpr (std::is_void_v<std::invoke_result_t<F, A...>>)
					{
						function(std::forward<A>(args)...);
						promise.set_value();
					}
					else
					{
						promise.set_value(function(std::forward<A>(args)...));
					}
				}
				catch (...)
				{
					promise.set_exception(std::current_exception());
				}
			};
		}
		//Var
		std::move_only_function<void()> executor_;
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

		template<typename F, typename ...A>
		auto Run(F&& function, A&& ...args)
		{
			auto [task, future] = tk::Task::Make(std::forward<F>(function), std::forward<A>(args)...);
			{
				std::lock_guard lk{ taskQueueMtx_ };
				tasks_.push_back(std::move(task));
			}
			taskQueueCV_.notify_one();
			return std::move(future);
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
	using namespace std::chrono_literals;
	tk::ThreadPool pool{ 4 };

	//Exceptions
	{
		const auto spit = [](int milliseconds) -> std::string
		{
			if (milliseconds && milliseconds % 100 == 0)
			{
				throw::std::runtime_error("ERROR");
			}
			std::this_thread::sleep_for(1ms * milliseconds);
			std::ostringstream ss;
			ss << std::this_thread::get_id();
			return ss.str();
		};

		auto futures = std::ranges::views::iota(0, 40) |
			std::ranges::views::transform([&](int i) {return pool.Run(spit, i * 25); }) |
			std::ranges::to<std::vector>();

		for (auto& f : futures)
		{
			try {
				std::cout << "<< " << f.get() << " >>" << std::endl;
			}
			catch (...)
			{
				std::cout << "exception caught" << std::endl;
			}
		}
	}

	//Polling
	{
		auto future = pool.Run([] {std::this_thread::sleep_for(2000ms); return 69; });
		while (future.wait_for(250ms) != std::future_status::ready)
		{
			std::cout << "Waiting..." << std::endl;
		}
		std::cout << "Task Ready! Value is: " << future.get() << std::endl;
	}

	return 0;
}