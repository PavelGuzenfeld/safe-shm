#pragma once
#include "safe-shm/config.hpp"
#include "safe-shm/double_buffer_swapper.hpp"
#include "safe-shm/flat_type.hpp"
#include "safe-shm/snapshot.hpp"
#include "safe-shm/swap_runner.hpp"
#include "safe-shm/logger.hpp"
#include "shm/semaphore.hpp"
#include "shm/shm.hpp"
#include <atomic>
#include <cassert>
#include <memory>

namespace safe_shm
{
    template <FlatType T>
    class DoubleBufferShm
    {
    public:
        explicit DoubleBufferShm(std::string const &shm_name,
                                 void (*log)(std::string_view) = default_logger)
            : shm_(shm::path(shm_name), sizeof(T)),
              sem_(shm_name + SEM_SUFFIX, SEM_INIT),
              pre_allocated_(std::make_unique<T>()),
              swapper_ptr_(nullptr),
              published_ptr_(nullptr),
              swapper_(&swapper_ptr_, pre_allocated_.get()),
              runner_(std::make_unique<SwapRunner>(
                  [this]
                  {
                      sem_.wait();
                      swapper_.swap();
                      published_ptr_.store(swapper_ptr_, std::memory_order_release);
                      sem_.post();
                  },
                  [log](std::string_view msg)
                  { log(msg); }))
        {
            swapper_.set_active(get_shm());
            published_ptr_.store(swapper_ptr_, std::memory_order_release);
        }

        ~DoubleBufferShm()
        {
            runner_.reset();
            sem_.destroy();
        }

        DoubleBufferShm(DoubleBufferShm const &) = delete;
        DoubleBufferShm &operator=(DoubleBufferShm const &) = delete;
        DoubleBufferShm(DoubleBufferShm &&) = delete;
        DoubleBufferShm &operator=(DoubleBufferShm &&) = delete;

        void store(T const &data)
        {
            sem_.wait();
            *get_shm() = data;
            sem_.post();
        }

        Snapshot<T> load()
        {
            swapper_.stage(get_shm());
            runner_->trigger();
            return Snapshot<T>{&published_ptr_};
        }

        void wait()
        {
            runner_->wait();
        }

        void const *shm_addr() const noexcept { return shm_.get(); }
        void const *pre_allocated_addr() const noexcept { return pre_allocated_.get(); }

    private:
        T *get_shm() const noexcept
        {
            auto *p = static_cast<T *>(shm_.get());
            assert(p && "shared memory data is null");
            return p;
        }

        shm::Shm shm_;
        shm::Semaphore sem_;
        std::unique_ptr<T> pre_allocated_;
        T *swapper_ptr_;
        std::atomic<T *> published_ptr_;
        DoubleBufferSwapper<T> swapper_;
        std::unique_ptr<SwapRunner> runner_;
    };
} // namespace safe_shm
