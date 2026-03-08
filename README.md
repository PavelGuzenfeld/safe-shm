# Thread safe shared memory
Thread-safe shared memory with double-buffered loading for lock-free reads.

Dependencies:
- See package.xml

Install via cmake:
```bash
mkdir build && cd build && cmake .. && make -j$(nproc) && sudo make install
```

## Example
```cpp
#include "safe-shm/dblbuf_loader.hpp"
#include "safe-shm/storage.hpp"
#include <fmt/core.h>

int main()
{
    constexpr auto shm_name = "test_shm";
    safe_shm::Storage<int> storage(shm_name);

    storage.store(42);

    safe_shm::DblBufLoader<int> loader(shm_name);
    auto snapshot = loader.load();
    loader.wait();  // block until data is copied to local buffer
    fmt::print("value: {}\n", *snapshot);
    return 0;
}
```

## Build with cmake
```cmake
cmake_minimum_required(VERSION 3.20)
project(safe_shm_test)

find_package(safe-shm REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} safe-shm::safe-shm)
```
