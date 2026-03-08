# safe-shm

Thread-safe shared memory with lock-free seqlock and double-buffered loading.

Header-only C++23 library. Uses sequence counters (seqlock) for near-zero-overhead reads, `std::jthread` for background swap operations, and Linux futex for cross-process synchronization. Works on x86_64 and ARM64.

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
| `shm_lock.hpp` | `ShmLock` | Cross-process futex mutex on shared memory |
| `double_buffer_swapper.hpp` | `DoubleBufferSwapper<T>` | Low-level buffer swap (memcpy to pre-allocated) |
| `swap_runner.hpp` | `SwapRunner` | Background `std::jthread` with atomic trigger/wait |
| `storage.hpp` | `Storage<T>` | Write-side shared memory (futex-locked) |
| `dblbuf_loader.hpp` | `DblBufLoader<T>` | Double-buffered async reader |
| `image_shm.hpp` | `DoubleBufferShm<T>` | Combined reader/writer with double buffering |
| `image.hpp` | `Image<W,H,TYPE>` | Compile-time image types (FHD/4K, RGB/RGBA/NV12) |
| `seqlock.hpp` | `SeqlockWriter<T>`, `SeqlockReader<T>`, `Seqlock<T>` | Lock-free seqlock (near-zero overhead) |
| `shared_memory.hpp` | `SharedMemory<T>` | Simple typed shm wrapper (no sync) |
| `producer_consumer.hpp` | `ProducerConsumer<T>` | Semaphore-based producer/consumer |

## Architecture

```
Writer (process A)                    Reader (process B)
┌──────────────┐                     ┌──────────────────────────┐
│  Storage<T>  │                     │    DblBufLoader<T>       │
│              │    /dev/shm/name    │                          │
│  store(data) ├───────────────────► │  load() → Snapshot<T>   │
│  [futex lock]│                     │  [stages shm pointer]   │
│  [memcpy]    │                     │  [triggers SwapRunner]  │
│  [futex unlock]                    │                          │
└──────────────┘                     │  SwapRunner (jthread)   │
                                     │  [futex lock]           │
                                     │  [swap to pre-alloc]    │
                                     │  [atomic publish]       │
                                     │  [futex unlock]         │
                                     │                          │
                                     │  wait() → done          │
                                     │  *snapshot → data        │
                                     └──────────────────────────┘
```

### Synchronization

**ShmLock** uses Linux futex on shared memory (`FUTEX_WAIT`/`FUTEX_WAKE`, not `_PRIVATE`) for cross-process locking. Uncontended lock is a single `atomic_ref::exchange` — no syscall. Only enters kernel when contended.

**SwapRunner** uses `std::atomic<uint32_t>::wait()`/`notify_one()` instead of mutex + condition_variable, eliminating per-cycle mutex overhead.

### Seqlock (lock-free fast path)

```
Writer                                  Reader
┌────────────────────┐                 ┌────────────────────┐
│ seq++ (odd)        │                 │ read seq (acquire)  │
│ memcpy data        │  /dev/shm/name  │ memcpy data         │
│ seq++ (even)       ├────────────────►│ read seq (acquire)  │
│ futex_wake         │                 │ if changed → retry  │
└────────────────────┘                 └────────────────────┘
```

- **Zero extra copies**: reader copies directly from shm (no intermediate buffer)
- **No mutex**: sequence counter detects torn reads, retry is rare
- **No background thread**: no `SwapRunner`, no `jthread`
- **Blocking mode**: `load_blocking()` uses raw futex to sleep until writer publishes

Use `Seqlock<T>` for same-process, `SeqlockWriter<T>` + `SeqlockReader<T>` for cross-process.

## Choosing a transport

| Need | Use |
|------|-----|
| Maximum throughput, single writer | `Seqlock<T>` / `SeqlockWriter<T>` + `SeqlockReader<T>` |
| Double-buffered async loading | `DoubleBufferShm<T>` / `Storage<T>` + `DblBufLoader<T>` |
| Simple typed shm (no sync) | `SharedMemory<T>` |
| Producer/consumer queue | `ProducerConsumer<T>` |

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

```cpp
// Lock-free seqlock (fastest, single writer)
#include "safe-shm/seqlock.hpp"

// Same process
safe_shm::Seqlock<int> sl("my_seqlock");
sl.store(42);
assert(sl.load() == 42);

// Cross-process: writer
safe_shm::SeqlockWriter<int> writer("my_seqlock");
writer.store(42);

// Cross-process: reader (blocking)
safe_shm::SeqlockReader<int> reader("my_seqlock");
uint32_t last_seq = 0;
int val = reader.load_blocking(last_seq); // sleeps until new data
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
| `seqlock_test` | Integration | Seqlock store/load, cross-process, concurrent, blocking |

## Benchmarks

Run: `./benchmark` (built with `-O3 -DNDEBUG`, uses [nanobench](https://github.com/martinus/nanobench))

| Method | 1 MB throughput | FHD RGB (6 MB) throughput |
|--------|----------------|--------------------------|
| memcpy (baseline) | 25.2 GB/s | 19.6 GB/s |
| raw POSIX shm (no sync) | 25.6 GB/s | 19.5 GB/s |
| **Seqlock (lock-free)** | **8.0 GB/s** | **22.4 GB/s** |
| SharedMemory (no sync) | 11.2 GB/s | - |
| DoubleBufferShm (futex) | 5.3 GB/s | 6.2 GB/s |
| pipe (kernel IPC) | 6.0 GB/s | - |

- **Seqlock**: 22.4 GB/s for FHD images — matches raw POSIX shm with full consistency guarantees
- **DoubleBufferShm**: 6.2 GB/s for FHD images (2.8x faster than POSIX semaphore version)
- **Seqlock vs DoubleBufferShm**: 2.9x faster at FHD — no background thread, no extra memcpy

## License

MIT
