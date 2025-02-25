#pragma once
#include "flat-type/flat.hpp"
#include "shm/semaphore.hpp"
#include "shm/shm.hpp"
#include "safe-shm/config.hpp"

namespace safe_shm
{
    template <FlatType T>
    class Storage
    {
    public:
        explicit Storage(std::string const &shm_name)
            : shm_(shm::path(shm_name), sizeof(T)),
              sem_(shm_name + SEM_SUFFIX, SEM_INIT)
        {
        }

        inline void store(T const &data)
        {
            sem_.wait();
            *static_cast<T *>(shm_.get()) = data;
            sem_.post();
        }

    private:
        shm::Shm shm_;
        shm::Semaphore sem_;
    };
} // namespace safe-shm