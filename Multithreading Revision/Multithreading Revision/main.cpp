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
			if (std::holds_alternative<std::monostate>(result_))
			{
				result_ = std::forward<R>(result);
				readySignal_.release();
			}
		}

		T Get()
		{
			readySignal_.acquire();
			if (auto ppException = std::get_if<std::exception_ptr>(&result_))
			{
				std::rethrow_exception(*ppException);
			}
			return std::move(std::get<T>(result_));
		}

		bool Ready()
		{
			if (readySignal_.try_acquire())
			{
				readySignal_.release();
				return true;
			}
			return false;
		}

	private:
		std::binary_semaphore readySignal_{ 0 }; //Similar to mutex but not tied to thread of execution, acquired on one thread and released on another
		std::variant<std::monostate, T, std::exception_ptr> result_;
	};

	template<>
	class SharedState<void>
	{
	public:
		void Set()
		{
			if (!complete_)
			{
				complete_ = true;
				readySignal_.release();
			}
		}

		void Set(std::exception_ptr pException)
		{
			if (!complete_)
			{
				complete_ = true;
				pException_ = pException;
				readySignal_.release();
			}
		}

		void Get()
		{
			readySignal_.acquire();
			if (pException_)
			{
				std::rethrow_exception(pException_);
			}
		}

		bool Ready()
		{
			if (readySignal_.try_acquire())
			{
				readySignal_.release();
				return true;
			}
			return false;
		}

	private:
		std::binary_semaphore readySignal_{ 0 }; //Similar to mutex but not tied to thread of execution, acquired on one thread and released on another
		bool complete_ = false;
		std::exception_ptr pException_ = nullptr;
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

		bool Ready()
		{
			return pState_->Ready();
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

		template<typename ...R>
		void Set(R&&... result)
		{
			pState_->Set(std::forward<R>(result)...);
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
				try {
					if constexpr (std::is_void_v<std::invoke_result_t<F, A...>>)
					{
						function(std::forward<A>(args)...);
						promise.Set();
					}
					else
					{
						promise.Set(function(std::forward<A>(args)...));
					}
				}
				catch (...)
				{
					promise.Set(std::current_exception());
				}
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

		template<typename F, typename ...A>
		auto Run(F&& function, A&& ...args)
		{
			auto [task, future] = tk::Task::Make(std::forward<F>(function), std::forward<A>(args)...);
			{
				std::lock_guard lk{ taskQueueMtx_ };
				tasks_.push_back(std::move(task));
			}
			taskQueueCV_.notify_one();
			return future;
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
				std::cout << "<< " << f.Get() << " >>" << std::endl;
			}
			catch (...)
			{
				std::cout << "exception caught" << std::endl;
			}
		}
	}

	//Future from promise
	{
		tk::Promise<int> prom;
		auto fut = prom.GetFuture();
		std::thread{ [](tk::Promise<int> p)
			{
				std::this_thread::sleep_for(2'500ms);
				p.Set(69);
			}, std::move(prom)
		}.detach();
		std::cout << fut.Get();
	}

	//Task Creation
	{
		auto [task, future] = tk::Task::Make([](int x)
			{
				std::this_thread::sleep_for(1'500ms);
				return x + 42000;
			}, 69);
		std::thread{ std::move(task) }.detach();
		std::cout << future.Get();
	}

	//Polling
	{
		auto future = pool.Run([] {std::this_thread::sleep_for(2000ms); return 69; });
		while (!future.Ready())
		{
			std::this_thread::sleep_for(250ms);
			std::cout << "Waiting..." << std::endl;
		}
		std::cout << "Task Ready! Value is: " << future.Get() << std::endl;
	}

	return 0;
}