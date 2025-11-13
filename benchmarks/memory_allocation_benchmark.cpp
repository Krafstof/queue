#include <benchmark/benchmark.h>
#include <vector>
#include <atomic>
#include <thread>
#include <random>
#include <chrono>
#include <new>         // for std::nothrow
#include <memory>
#include <iostream>

// =====================================
// Simple message structure (heap-heavy)
// =====================================
struct Message {
    std::vector<uint8_t> payload;
    Message(size_t size) : payload(size, 0xAB) {}
};

// =====================================
// Lock-free Single Producer Single Consumer Queue (simplified)
// =====================================
template <typename T, size_t Capacity>
class SPSCQueue {
public:
    SPSCQueue() : head_(0), tail_(0) {
        buffer_.resize(Capacity);
    }

    bool push(std::unique_ptr<T> item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t next = (head + 1) % Capacity;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(std::unique_ptr<T>& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        item = std::move(buffer_[tail]);
        tail_.store((tail + 1) % Capacity, std::memory_order_release);
        return true;
    }

private:
    std::vector<std::unique_ptr<T>> buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

// =====================================
// Utility: approximate memory usage
// =====================================
static size_t getMemoryUsageBytes() {
    long rss = 0L;
    FILE* fp = nullptr;
    if ((fp = fopen("/proc/self/statm", "r")) == nullptr)
        return 0L;
    if (fscanf(fp, "%*s%ld", &rss) != 1) {
        fclose(fp);
        return 0L;
    }
    fclose(fp);
    return (size_t)rss * (size_t)sysconf(_SC_PAGESIZE);
}

// =====================================
// Benchmark: Allocation patterns & memory usage
// =====================================

// Each state.range(0) is payload size, range(1) is queue capacity
static void BM_MemoryAllocation_SPSCQueue(benchmark::State& state) {
    const size_t payload_size = state.range(0);
    const size_t queue_capacity = state.range(1);

    SPSCQueue<Message, 1 << 16> queue;
    std::atomic<bool> stop_flag{false};
    std::atomic<size_t> produced{0}, consumed{0};

    std::thread producer([&]() {
        std::mt19937 gen(42);
        while (!stop_flag.load()) {
            auto msg = std::make_unique<Message>(payload_size);
            if (queue.push(std::move(msg))) {
                produced++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        std::unique_ptr<Message> msg;
        while (!stop_flag.load()) {
            if (queue.pop(msg)) {
                consumed++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    for (auto _ : state) {
        auto before_mem = getMemoryUsageBytes();
        auto start = std::chrono::steady_clock::now();

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto end = std::chrono::steady_clock::now();
        auto after_mem = getMemoryUsageBytes();

        double elapsed = std::chrono::duration<double>(end - start).count();
        size_t used_mem = (after_mem > before_mem) ? (after_mem - before_mem) : 0;

        state.counters["Mem_Bytes"] = static_cast<double>(used_mem);
        state.counters["Produced"] = static_cast<double>(produced.load());
        state.counters["Consumed"] = static_cast<double>(consumed.load());
        state.counters["Payload_Size"] = static_cast<double>(payload_size);
        state.counters["Queue_Capacity"] = static_cast<double>(queue_capacity);
        state.counters["Alloc_Rate"] = static_cast<double>(produced.load()) / elapsed;
    }

    stop_flag = true;
    producer.join();
    consumer.join();
}

// =====================================
// Register benchmarks
// =====================================
// Range(0, payload size), Range(1, queue capacity)
BENCHMARK(BM_MemoryAllocation_SPSCQueue)
    ->Args({64, 1024})      // small payload, small queue
    ->Args({1024, 1024})    // medium payload
    ->Args({8192, 1024})    // large payload
    ->Args({1024, 1 << 14}) // medium payload, big queue
    ->Iterations(3)
    ->UseRealTime();

BENCHMARK_MAIN();
