#pragma once
#include "double-buffer-swapper/swapper.hpp"
#include "flat-type/flat.hpp"
#include "shm/semaphore.hpp"
#include "shm/shm.hpp"
#include "single-task-runner/runner.hpp"
#include "safe-shm/config.hpp"
#include <atomic>
#include <cassert>

namespace safe_shm
{
    template <FlatType T>
    class Snapshot
    {
    public:
        explicit Snapshot(std::atomic<T *> *ptr) noexcept : ptr_(ptr) {}

        T const &operator*() const noexcept
        {
            auto *p = ptr_->load(std::memory_order_acquire);
            assert(p && "snapshot data is null");
            return *p;
        }

        T const *operator->() const noexcept
        {
            auto *p = ptr_->load(std::memory_order_acquire);
            assert(p && "snapshot data is null");
            return p;
        }

        T *get() const noexcept
        {
            return ptr_->load(std::memory_order_acquire);
        }

    private:
        std::atomic<T *> *ptr_;
    };

    void logger(std::string_view msg) noexcept;

    template <FlatType T>
    class DblBufLoader
    {
    public:
        explicit DblBufLoader(std::string const &shm_name, void (*log)(std::string_view) = logger)
            : shm_(shm::path(shm_name), sizeof(T)),
              sem_(shm_name + SEM_SUFFIX, SEM_INIT),
              pre_allocated_(std::make_unique<T>()),
              swapper_ptr_(nullptr),
              published_ptr_(nullptr)
        {
            swapper_ = std::make_unique<DoubleBufferSwapper<T>>(&swapper_ptr_, pre_allocated_.get());
            runner_ = std::make_unique<run::SingleTaskRunner>(
                [this]
                {
                    sem_.wait();
                    swapper_->swap();
                    published_ptr_.store(swapper_ptr_, std::memory_order_release);
                    sem_.post();
                },
                [log](std::string_view msg)
                { log(msg); });
            runner_->async_start();
            swapper_->set_active(get_shm());
            published_ptr_.store(swapper_ptr_, std::memory_order_release);
        }

        ~DblBufLoader()
        {
            runner_->async_stop();
            sem_.destroy();
        }

        DblBufLoader(DblBufLoader const &) = delete;
        DblBufLoader &operator=(DblBufLoader const &) = delete;
        DblBufLoader(DblBufLoader &&) = delete;
        DblBufLoader &operator=(DblBufLoader &&) = delete;

        Snapshot<T> load()
        {
            auto *shm = get_shm();
            assert(shm && "shared memory data is null");
            swapper_->stage(shm);
            runner_->trigger_once();
            return Snapshot<T>{&published_ptr_};
        }

        void wait()
        {
            runner_->wait_for_all_tasks();
        }

    private:
        T *get_shm() const noexcept
        {
            return static_cast<T *>(shm_.get());
        }

        shm::Shm shm_;
        shm::Semaphore sem_;
        std::unique_ptr<T> pre_allocated_;
        std::unique_ptr<DoubleBufferSwapper<T>> swapper_;
        std::unique_ptr<run::SingleTaskRunner> runner_;
        T *swapper_ptr_;
        std::atomic<T *> published_ptr_;
    };
} // namespace safe_shm
