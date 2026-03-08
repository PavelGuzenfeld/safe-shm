# safe-shm

Thread-safe shared memory with double-buffered loading for lock-free reads.

Consolidates `flat-type`, `double-buffer-swapper`, `single-task-runner`, and `image-shm-dblbuf` into a single library. Uses `std::jthread` instead of an external task runner.

## Dependencies

- `shm` — POSIX shared memory wrapper
- `exception-rt` — runtime exception utilities
- `fmt` — formatting
- `nanobind` (optional) — Python bindings

## Components

| Header | Description |
|--------|-------------|
| `flat_type.hpp` | `FlatType` concept (trivially copyable + standard layout) |
| `snapshot.hpp` | `Snapshot<T>` — atomic, race-free read handle |
| `storage.hpp` | `Storage<T>` — write-side shared memory |
| `dblbuf_loader.hpp` | `DblBufLoader<T>` — double-buffered reader |
| `image_shm.hpp` | `DoubleBufferShm<T>` — combined read/write with double buffering |
| `image.hpp` | Compile-time `Image<W,H,TYPE>` (FHD/4K, RGB/RGBA/NV12) |
| `shared_memory.hpp` | `SharedMemory<T>` — simple typed shm wrapper |
| `producer_consumer.hpp` | `ProducerConsumer<T>` — semaphore-based producer/consumer |

## Example

```cpp
#include "safe-shm/dblbuf_loader.hpp"
#include "safe-shm/storage.hpp"

int main()
{
    safe_shm::Storage<int> storage("my_shm");
    storage.store(42);

    safe_shm::DblBufLoader<int> loader("my_shm");
    auto snapshot = loader.load();
    loader.wait();
    // *snapshot == 42
}
```

## Build

```bash
mkdir build && cd build && cmake .. && make -j$(nproc) && ctest
```
