#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string_view>
#include <thread>

namespace safe_shm
{
    class SwapRunner
    {
    public:
        using task_fn = std::function<void()>;
        using log_fn = std::function<void(std::string_view)>;

        SwapRunner(task_fn task, log_fn log)
            : task_(std::move(task)),
              log_(std::move(log)),
              thread_([this](std::stop_token st) { run(st); })
        {
        }

        SwapRunner(SwapRunner const &) = delete;
        SwapRunner &operator=(SwapRunner const &) = delete;
        SwapRunner(SwapRunner &&) = delete;
        SwapRunner &operator=(SwapRunner &&) = delete;

        void trigger() noexcept
        {
            {
                std::lock_guard lock(mutex_);
                triggered_ = true;
                idle_ = false;
            }
            trigger_cv_.notify_one();
        }

        void wait() noexcept
        {
            std::unique_lock lock(mutex_);
            done_cv_.wait(lock, [this] { return idle_; });
        }

    private:
        void run(std::stop_token st) noexcept
        {
            while (!st.stop_requested())
            {
                std::unique_lock lock(mutex_);
                trigger_cv_.wait(lock, st, [this] { return triggered_; });
                if (st.stop_requested())
                    break;
                triggered_ = false;
                lock.unlock();

                try
                {
                    task_();
                }
                catch (...)
                {
                    log_("SwapRunner: task threw exception\n");
                }

                {
                    std::lock_guard lk(mutex_);
                    idle_ = true;
                }
                done_cv_.notify_all();
            }
        }

        task_fn task_;
        log_fn log_;
        std::mutex mutex_;
        std::condition_variable_any trigger_cv_;
        std::condition_variable done_cv_;
        bool triggered_ = false;
        bool idle_ = true;
        std::jthread thread_; // must be last — starts thread in constructor
    };
} // namespace safe_shm
