#include "safe-shm/dblbuf_loader.hpp"
#include <fmt/core.h>

namespace safe_shm
{
    void logger(std::string_view msg) noexcept
    {
        fmt::print("{}", msg);
    }
} // namespace safe_shm
