# safe-shm

Thread-safe shared memory with double-buffered loading for lock-free reads.

Consolidates `flat-type`, `double-buffer-swapper`, `single-task-runner`, and `image-shm-dblbuf` into a single header-only C++23 library. Uses `std::jthread` for background swap operations.

## Dependencies

- [`shm`](https://github.com/PavelGuzenfeld/shm) — POSIX shared memory wrapper
- [`exception-rt`](https://github.com/PavelGuzenfeld/exception-rt) — runtime exception utilities
- [`fmt`](https://github.com/fmtlib/fmt) — formatting
- [`nanobind`](https://github.com/wjakob/nanobind) (optional) — Python bindings

## Components

| Header | Class | Description |
|--------|-------|-------------|
| `flat_type.hpp` | `FlatType` | Concept: trivially copyable + standard layout |
| `snapshot.hpp` | `Snapshot<T>` | Atomic acquire-semantics read handle |
| `double_buffer_swapper.hpp` | `DoubleBufferSwapper<T>` | Low-level buffer swap (memcpy to pre-allocated) |
| `swap_runner.hpp` | `SwapRunner` | Background `std::jthread` with trigger/wait |
| `storage.hpp` | `Storage<T>` | Write-side shared memory (semaphore-protected) |
| `dblbuf_loader.hpp` | `DblBufLoader<T>` | Double-buffered async reader |
| `image_shm.hpp` | `DoubleBufferShm<T>` | Combined reader/writer with double buffering |
| `image.hpp` | `Image<W,H,TYPE>` | Compile-time image types (FHD/4K, RGB/RGBA/NV12) |
| `shared_memory.hpp` | `SharedMemory<T>` | Simple typed shm wrapper (no sync) |
| `producer_consumer.hpp` | `ProducerConsumer<T>` | Semaphore-based producer/consumer |

## Architecture

```
Writer (process A)                    Reader (process B)
┌──────────────┐                     ┌──────────────────────────┐
│  Storage<T>  │                     │    DblBufLoader<T>       │
│              │    /dev/shm/name    │                          │
│  store(data) ├───────────────────► │  load() → Snapshot<T>   │
│  [sem.wait]  │                     │  [stages shm pointer]   │
│  [memcpy]    │                     │  [triggers SwapRunner]  │
│  [sem.post]  │                     │                          │
└──────────────┘                     │  SwapRunner (jthread)   │
                                     │  [sem.wait]             │
                                     │  [swap to pre-alloc]    │
                                     │  [atomic publish]       │
                                     │  [sem.post]             │
                                     │                          │
                                     │  wait() → done          │
                                     │  *snapshot → data        │
                                     └──────────────────────────┘
```

## Example

```cpp
// Writer process
#include "safe-shm/storage.hpp"

safe_shm::Storage<int> storage("my_shm");
storage.store(42);
```

```cpp
// Reader process
#include "safe-shm/dblbuf_loader.hpp"

safe_shm::DblBufLoader<int> loader("my_shm");
auto snapshot = loader.load();
loader.wait();
assert(*snapshot == 42);
```

```cpp
// Combined reader/writer (e.g., image pipeline)
#include "safe-shm/image_shm.hpp"
#include "safe-shm/image.hpp"

using Image = safe_shm::img::ImageFHD_RGB;
safe_shm::DoubleBufferShm<Image> shm("camera_feed");

Image frame{};
frame.timestamp = 1000;
shm.store(frame);

auto snap = shm.load();
shm.wait();
assert(snap->timestamp == 1000);
```

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest
```

### Sanitizers

```bash
# Address + UB sanitizer (default)
cmake .. -DSANITIZER=asan

# Thread sanitizer
cmake .. -DSANITIZER=tsan

# No sanitizer
cmake .. -DSANITIZER=none
```

### Docker (CI)

```bash
docker build -t safe-shm-test -f Dockerfile.test .
docker run --rm -v $(pwd):/src safe-shm-test bash -c \
  "cd /tmp && cmake /src -DSANITIZER=none && make -j\$(nproc) && ctest"
```

## Tests

| Test | Type | What it covers |
|------|------|---------------|
| `flat_type_test` | Unit | FlatType concept acceptance/rejection |
| `double_buffer_swapper_test` | Unit | Swap mechanics, structs, large arrays |
| `swap_runner_test` | Unit | Trigger/wait, exceptions, concurrent threads |
| `safe_shm_test` | Integration | Storage + DblBufLoader round-trip |
| `shared_memory_test` | Integration | SharedMemory typed wrapper |
| `image_shm_test` | Integration | DoubleBufferShm with FHD images |
| `integration_test` | Integration | Cross-process (fork) all components |

## Benchmarks

Run: `./benchmark` (built with `-O3 -DNDEBUG`, no sanitizers)

| Method | 64 B | 4 KB | 1 MB | 6 MB (FHD) |
|--------|------|------|------|------------|
| memcpy (baseline) | 0.0 us | 0.1 us | 78 us | 1057 us |
| raw POSIX shm | 0.0 us | 0.1 us | 82 us | 1051 us |
| SharedMemory | 0.0 us | 0.1 us | 80 us | - |
| DoubleBufferShm | 6.8 us | 7.1 us | 208 us | 2755 us |
| pipe (kernel IPC) | 0.9 us | 1.0 us | 437 us | - |

- **SharedMemory**: zero overhead vs raw POSIX shm
- **DoubleBufferShm**: ~7us fixed cost (semaphore + thread wake), 2.1 GB/s at FHD
- **2-5x faster than pipe** at all payload sizes

## License

MIT
