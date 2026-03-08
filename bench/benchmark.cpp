#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "safe-shm/dblbuf_loader.hpp"
#include "safe-shm/image.hpp"
#include "safe-shm/image_shm.hpp"
#include "safe-shm/producer_consumer.hpp"
#include "safe-shm/shared_memory.hpp"
#include "safe-shm/storage.hpp"

#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fmt/format.h>
#include <numeric>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// ─── Helpers ─────────────────────────────────────────────────────────────────

struct BenchResult
{
    double mean_us;
    double median_us;
    double min_us;
    double max_us;
    double p99_us;
    double throughput_mb_s;
};

template <typename Fn>
static std::vector<double> measure(Fn &&fn, int iterations)
{
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(iterations));

    for (int i = 0; i < iterations; ++i)
    {
        auto t0 = std::chrono::steady_clock::now();
        fn();
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        times.push_back(us);
    }
    return times;
}

static BenchResult summarize(std::vector<double> &times, std::size_t payload_bytes)
{
    std::sort(times.begin(), times.end());
    auto n = times.size();
    double sum = std::accumulate(times.begin(), times.end(), 0.0);
    double mean = sum / static_cast<double>(n);
    double median = times[n / 2];
    double min = times.front();
    double max = times.back();
    double p99 = times[static_cast<std::size_t>(static_cast<double>(n) * 0.99)];
    double throughput = (mean > 0.0)
                            ? (static_cast<double>(payload_bytes) / (mean * 1e-6)) / (1024.0 * 1024.0)
                            : 0.0;
    return {mean, median, min, max, p99, throughput};
}

static void print_result(char const *label, BenchResult const &r, std::size_t bytes)
{
    fmt::print("  {:40s}  payload={:>9}  mean={:8.1f}us  median={:8.1f}us  "
               "min={:8.1f}us  p99={:8.1f}us  throughput={:8.1f} MB/s\n",
               label, fmt::format("{} B", bytes),
               r.mean_us, r.median_us, r.min_us, r.p99_us, r.throughput_mb_s);
}

// ─── Raw POSIX shm baseline ─────────────────────────────────────────────────

class RawPosixShm
{
public:
    explicit RawPosixShm(char const *name, std::size_t size)
        : name_(name), size_(size)
    {
        fd_ = shm_open(name, O_CREAT | O_RDWR, 0666);
        if (fd_ < 0)
            throw std::runtime_error("shm_open failed");
        if (ftruncate(fd_, static_cast<off_t>(size)) < 0)
            throw std::runtime_error("ftruncate failed");
        ptr_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (ptr_ == MAP_FAILED)
            throw std::runtime_error("mmap failed");
    }

    ~RawPosixShm()
    {
        if (ptr_ && ptr_ != MAP_FAILED)
            munmap(ptr_, size_);
        if (fd_ >= 0)
            close(fd_);
        shm_unlink(name_.c_str());
    }

    void *get() noexcept { return ptr_; }
    std::size_t size() const noexcept { return size_; }

    RawPosixShm(RawPosixShm const &) = delete;
    RawPosixShm &operator=(RawPosixShm const &) = delete;

private:
    std::string name_;
    std::size_t size_;
    int fd_{-1};
    void *ptr_{nullptr};
};

// ─── Payload types ──────────────────────────────────────────────────────────

struct SmallPayload
{
    double values[8]; // 64 bytes
};

struct MediumPayload
{
    uint8_t data[4096]; // 4 KB
};

struct LargePayload
{
    uint8_t data[1024 * 1024]; // 1 MB
};

static_assert(sizeof(SmallPayload) == 64);
static_assert(sizeof(MediumPayload) == 4096);
static_assert(sizeof(LargePayload) == 1024 * 1024);

// ─── Benchmarks ─────────────────────────────────────────────────────────────

constexpr int WARMUP = 50;
constexpr int ITERATIONS = 1000;

TEST_CASE("BENCHMARK: memcpy baseline")
{
    fmt::print("\n=== memcpy baseline (local memory, no IPC) ===\n");

    auto run = [](char const *label, std::size_t size)
    {
        auto src = std::make_unique<uint8_t[]>(size);
        auto dst = std::make_unique<uint8_t[]>(size);
        std::memset(src.get(), 0xAA, size);

        // warmup
        for (int i = 0; i < WARMUP; ++i)
            std::memcpy(dst.get(), src.get(), size);

        auto times = measure([&]
                             { std::memcpy(dst.get(), src.get(), size); }, ITERATIONS);
        auto r = summarize(times, size);
        print_result(label, r, size);
        CHECK(r.mean_us > 0.0);
    };

    run("memcpy 64 B", 64);
    run("memcpy 4 KB", 4096);
    run("memcpy 1 MB", 1024 * 1024);
    run("memcpy 6 MB (FHD RGB)", sizeof(safe_shm::img::ImageFHD_RGB));
}

TEST_CASE("BENCHMARK: raw POSIX shm (mmap + memcpy)")
{
    fmt::print("\n=== raw POSIX shm (mmap + memcpy, no sync) ===\n");

    auto run = [](char const *label, std::size_t size)
    {
        RawPosixShm shm("/bench_raw_posix", size);
        auto src = std::make_unique<uint8_t[]>(size);
        std::memset(src.get(), 0xBB, size);

        for (int i = 0; i < WARMUP; ++i)
            std::memcpy(shm.get(), src.get(), size);

        auto times = measure([&]
                             { std::memcpy(shm.get(), src.get(), size); }, ITERATIONS);
        auto r = summarize(times, size);
        print_result(label, r, size);
        CHECK(r.mean_us > 0.0);
    };

    run("raw posix shm 64 B", 64);
    run("raw posix shm 4 KB", 4096);
    run("raw posix shm 1 MB", 1024 * 1024);
    run("raw posix shm 6 MB (FHD RGB)", sizeof(safe_shm::img::ImageFHD_RGB));
}

TEST_CASE("BENCHMARK: SharedMemory<T> (safe-shm)")
{
    fmt::print("\n=== SharedMemory<T> (safe-shm, no sync) ===\n");

    {
        safe_shm::SharedMemory<SmallPayload> shm("bench_shmem_small");
        SmallPayload src{};
        std::fill(std::begin(src.values), std::end(src.values), 3.14);

        for (int i = 0; i < WARMUP; ++i)
            shm.get() = src;

        auto times = measure([&]
                             { shm.get() = src; }, ITERATIONS);
        auto r = summarize(times, sizeof(SmallPayload));
        print_result("SharedMemory 64 B", r, sizeof(SmallPayload));
    }
    {
        safe_shm::SharedMemory<MediumPayload> shm("bench_shmem_med");
        MediumPayload src{};
        std::memset(src.data, 0xCC, sizeof(src.data));

        for (int i = 0; i < WARMUP; ++i)
            shm.get() = src;

        auto times = measure([&]
                             { shm.get() = src; }, ITERATIONS);
        auto r = summarize(times, sizeof(MediumPayload));
        print_result("SharedMemory 4 KB", r, sizeof(MediumPayload));
    }
    {
        safe_shm::SharedMemory<LargePayload> shm("bench_shmem_large");
        LargePayload src{};
        std::memset(src.data, 0xDD, sizeof(src.data));

        for (int i = 0; i < WARMUP; ++i)
            shm.get() = src;

        auto times = measure([&]
                             { shm.get() = src; }, ITERATIONS);
        auto r = summarize(times, sizeof(LargePayload));
        print_result("SharedMemory 1 MB", r, sizeof(LargePayload));
    }
    CHECK(true);
}

TEST_CASE("BENCHMARK: Storage + DblBufLoader (safe-shm)")
{
    fmt::print("\n=== Storage + DblBufLoader (semaphore + double-buffer) ===\n");

    auto run = [](auto const &label, auto const &shm_name, auto const &src)
    {
        using T = std::decay_t<decltype(src)>;
        safe_shm::Storage<T> storage(shm_name);
        safe_shm::DblBufLoader<T> loader(shm_name,
                                         [](std::string_view) {});

        // warmup
        for (int i = 0; i < WARMUP; ++i)
        {
            storage.store(src);
            [[maybe_unused]] auto snap = loader.load();
            loader.wait();
        }

        auto times = measure([&]
                             {
            storage.store(src);
            [[maybe_unused]] auto snap = loader.load();
            loader.wait(); }, ITERATIONS);
        auto r = summarize(times, sizeof(T));
        print_result(label, r, sizeof(T));
    };

    SmallPayload small{};
    std::fill(std::begin(small.values), std::end(small.values), 1.0);
    run("Storage+DblBufLoader 64 B", "bench_dblbuf_s", small);

    MediumPayload medium{};
    std::memset(medium.data, 0xAA, sizeof(medium.data));
    run("Storage+DblBufLoader 4 KB", "bench_dblbuf_m", medium);

    LargePayload large{};
    std::memset(large.data, 0xBB, sizeof(large.data));
    run("Storage+DblBufLoader 1 MB", "bench_dblbuf_l", large);

    CHECK(true);
}

TEST_CASE("BENCHMARK: DoubleBufferShm<T> (safe-shm)")
{
    fmt::print("\n=== DoubleBufferShm<T> (store + load cycle) ===\n");

    auto run = [](auto const &label, auto const &shm_name, auto const &src)
    {
        using T = std::decay_t<decltype(src)>;
        safe_shm::DoubleBufferShm<T> shm(shm_name,
                                          [](std::string_view) {});

        for (int i = 0; i < WARMUP; ++i)
        {
            shm.store(src);
            [[maybe_unused]] auto snap = shm.load();
            shm.wait();
        }

        auto times = measure([&]
                             {
            shm.store(src);
            [[maybe_unused]] auto snap = shm.load();
            shm.wait(); }, ITERATIONS);
        auto r = summarize(times, sizeof(T));
        print_result(label, r, sizeof(T));
    };

    SmallPayload small{};
    std::fill(std::begin(small.values), std::end(small.values), 2.0);
    run("DoubleBufferShm 64 B", "bench_dbshm_s", small);

    MediumPayload medium{};
    std::memset(medium.data, 0xCC, sizeof(medium.data));
    run("DoubleBufferShm 4 KB", "bench_dbshm_m", medium);

    LargePayload large{};
    std::memset(large.data, 0xDD, sizeof(large.data));
    run("DoubleBufferShm 1 MB", "bench_dbshm_l", large);

    CHECK(true);
}

TEST_CASE("BENCHMARK: pipe baseline (IPC comparison)")
{
    fmt::print("\n=== pipe (kernel IPC baseline) ===\n");

    auto run = [](char const *label, std::size_t size)
    {
        auto buf = std::make_unique<uint8_t[]>(size);
        auto recv = std::make_unique<uint8_t[]>(size);
        std::memset(buf.get(), 0xEE, size);

        int fds[2];
        if (pipe(fds) < 0)
            throw std::runtime_error("pipe failed");

        // Set pipe size large enough
        fcntl(fds[0], F_SETPIPE_SZ, static_cast<int>(std::max(size, std::size_t{65536})));

        // warmup
        for (int i = 0; i < WARMUP; ++i)
        {
            auto written = write(fds[1], buf.get(), size);
            auto readn = read(fds[0], recv.get(), size);
            (void)written;
            (void)readn;
        }

        auto times = measure([&]
                             {
            auto w = write(fds[1], buf.get(), size);
            auto r = read(fds[0], recv.get(), size);
            (void)w; (void)r; }, ITERATIONS);
        auto r = summarize(times, size);
        print_result(label, r, size);

        close(fds[0]);
        close(fds[1]);
    };

    run("pipe 64 B", 64);
    run("pipe 4 KB", 4096);
    run("pipe 1 MB", 1024 * 1024);
    CHECK(true);
}

TEST_CASE("BENCHMARK: cross-process Storage+DblBufLoader latency")
{
    fmt::print("\n=== cross-process latency (fork, store in child, load in parent) ===\n");

    constexpr auto shm_name = "bench_xproc";
    constexpr int XPROC_ITERS = 200;

    // Pre-create the shared memory
    {
        safe_shm::Storage<SmallPayload> storage(shm_name);
        SmallPayload init{};
        storage.store(init);
    }

    std::vector<double> times;
    times.reserve(XPROC_ITERS);

    for (int i = 0; i < XPROC_ITERS; ++i)
    {
        auto t0 = std::chrono::steady_clock::now();

        pid_t pid = fork();
        if (pid == 0)
        {
            safe_shm::Storage<SmallPayload> storage(shm_name);
            SmallPayload data{};
            data.values[0] = static_cast<double>(i);
            storage.store(data);
            _exit(0);
        }

        int status;
        waitpid(pid, &status, 0);

        safe_shm::DblBufLoader<SmallPayload> loader(shm_name,
                                                     [](std::string_view) {});
        [[maybe_unused]] auto snap = loader.load();
        loader.wait();

        auto t1 = std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    auto r = summarize(times, sizeof(SmallPayload));
    print_result("cross-process 64 B (fork+store+load)", r, sizeof(SmallPayload));
    CHECK(r.mean_us > 0.0);
}

TEST_CASE("BENCHMARK: FHD image throughput")
{
    fmt::print("\n=== FHD RGB image (~6 MB) throughput ===\n");

    constexpr int IMG_ITERS = 100;
    using Image = safe_shm::img::ImageFHD_RGB;

    // DoubleBufferShm store+load
    {
        safe_shm::DoubleBufferShm<Image> shm("bench_fhd_img",
                                              [](std::string_view) {});
        auto img = std::make_unique<Image>();
        img->timestamp = 1;
        img->frame_number = 1;
        std::fill(img->data.begin(), img->data.end(), 0xFF);

        for (int i = 0; i < 10; ++i)
        {
            shm.store(*img);
            [[maybe_unused]] auto snap = shm.load();
            shm.wait();
        }

        auto times = measure([&]
                             {
            shm.store(*img);
            [[maybe_unused]] auto snap = shm.load();
            shm.wait(); }, IMG_ITERS);
        auto r = summarize(times, sizeof(Image));
        print_result("DoubleBufferShm FHD RGB", r, sizeof(Image));
    }

    // Raw POSIX shm comparison
    {
        RawPosixShm raw("/bench_fhd_raw", sizeof(Image));
        auto img = std::make_unique<Image>();
        img->timestamp = 2;
        img->frame_number = 2;
        std::fill(img->data.begin(), img->data.end(), 0xAA);

        for (int i = 0; i < 10; ++i)
            std::memcpy(raw.get(), img.get(), sizeof(Image));

        auto times = measure([&]
                             { std::memcpy(raw.get(), img.get(), sizeof(Image)); }, IMG_ITERS);
        auto r = summarize(times, sizeof(Image));
        print_result("raw POSIX shm FHD RGB", r, sizeof(Image));
    }

    CHECK(true);
}

TEST_CASE("BENCHMARK: summary comparison table")
{
    fmt::print("\n");
    fmt::print("┌──────────────────────────────────────────────────────────────┐\n");
    fmt::print("│                    Benchmark Summary                        │\n");
    fmt::print("├──────────────────────────────────────────────────────────────┤\n");
    fmt::print("│ memcpy           = theoretical max (no IPC, no sync)        │\n");
    fmt::print("│ raw POSIX shm    = mmap+memcpy (no sync, no safety)         │\n");
    fmt::print("│ SharedMemory     = safe-shm thin wrapper (no sync)          │\n");
    fmt::print("│ pipe             = kernel IPC baseline (copy-based)         │\n");
    fmt::print("│ Storage+DblBuf   = safe-shm with semaphore + double-buffer │\n");
    fmt::print("│ DoubleBufferShm  = safe-shm combined store+load            │\n");
    fmt::print("│ cross-process    = fork + store + load end-to-end           │\n");
    fmt::print("└──────────────────────────────────────────────────────────────┘\n");
    fmt::print("\nOverhead = (safe-shm latency / raw shm latency) - 1\n");
    fmt::print("Lower overhead = better. <10%% overhead at 1MB+ is excellent.\n\n");
    CHECK(true);
}
