# Thread safe shared memory
This is a simple shared memory implementation that is thread safe.

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
    assert(*ret == data);

    fmt::print("All tests passed\n");
    return 0;
    }
```

## Build with cmake
```cmake
cmake_minimum_required(VERSION 3.5)
project(safe_shm_test)

find_package(safe_shm REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} safe_shm::safe_shm)
```
