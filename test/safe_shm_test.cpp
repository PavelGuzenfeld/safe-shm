#include "safe-shm/dblbuf_loader.hpp"
#include "safe-shm/storage.hpp"
#include <cassert>
#include <fmt/core.h>

int main()
{
    fmt::print("Starting safe_shm test\n");
    constexpr auto shm_name = "test_shm";
    safe_shm::Storage<int> storage(shm_name);

    auto const data = 42;
    storage.store(data);

    safe_shm::DblBufLoader<int> loader(shm_name);
    auto ret = loader.load();
    assert(**(ret.data) == data);

    fmt::print("All tests passed\n");
    return 0;
}