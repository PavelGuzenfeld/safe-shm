#pragma once
#include "double-buffer-swapper/swapper.hpp"
#include "flat-type/flat.hpp"
#include "image-shm-dblbuf/image.hpp"
#include "shm/semaphore.hpp"
#include "shm/shm.hpp"
#include "single-task-runner/runner.hpp"
#include "safe-shm/config.hpp"

namespace safe_shm
{
    template <FlatType T>
    struct RetrunType
    {
        T **data = nullptr;
    };

    template <FlatType T>
    class DblBufLoader
    {
    public:
        DblBufLoader(std::string const &shm_name)
            : shm_(shm::path(shm_name), sizeof(T)),
              sem_(shm_name + SEM_SUFFIX, SEM_INIT),
              pre_allocated_(std::make_unique<T>()),
              img_ptr_(nullptr),
              return_ptr_{&img_ptr_}
        {
            swapper_ = std::make_unique<DoubleBufferSwapper<T>>(&img_ptr_, pre_allocated_.get());
            runner_ = std::make_unique<run::SingleTaskRunner>([&]
                                                              {
                                                            sem_.wait();
                                                            swapper_->swap();
                                                            sem_.post(); },
                                                              [&](std::string_view msg)
                                                              { log(msg); });
            runner_->async_start();
            swapper_->set_active(get_shm());
        }

        ~DblBufLoader()
        {
            runner_->async_stop();
            sem_.destroy();
            return_ptr_.img_ptr_ = nullptr;
        }

        DblBufLoader(DblBufLoader const &) = delete;
        DblBufLoader &operator=(DblBufLoader const &) = delete;
        DblBufLoader(DblBufLoader &&) = delete;
        DblBufLoader &operator=(DblBufLoader &&) = delete;

        RetrunType load()
        {
            auto *img = get_shm();
            assert(img && "shared memory data is null");
            swapper_->stage(img);
            runner_->trigger_once();
            return return_ptr_;
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
        T *img_ptr_;
        RetrunType return_ptr_;
    };
} // namespace safe_shm