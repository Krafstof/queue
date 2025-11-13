#include <benchmark/benchmark.h>
#include <thread>
#include <atomic>
#include <vector>
#include <array>
#include <cstdint>
#include <random>

// ==========================================================
// Lock-Free SPSC Queue
// ==========================================================
template <typename T, size_t Capacity>
class SPSCQueue {
public:
    SPSCQueue() : head_(0), tail_(0) {}

    bool push(const T& item) {
        auto head = head_.load(std::memory_order_relaxed);
        auto next = (head + 1) % Capacity;
        if (next == tail_.load(std::memory_order_acquire))
            return false; // full
        buffer_[head] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false; // empty
        item = buffer_[tail];
        tail_.store((tail + 1) % Capacity, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

// ==========================================================
// Message
// ==========================================================
struct Message {
    uint64_t timestamp;
    uint32_t value;
};

constexpr size_t QUEUE_SIZE = 1 << 16;

// ==========================================================
// Benchmark: SPSC Queue Throughput
// ==========================================================
static void BM_SPSCQueue_Throughput(benchmark::State& state) {
    constexpr int batch_size = 1000; // messages per benchmark iteration
    SPSCQueue<Message, QUEUE_SIZE> queue;

    std::atomic<bool> start_flag{false};
    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> count{0};

    // Producer thread
    std::thread producer([&]() {
        Message msg{};
        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!stop_flag.load(std::memory_order_relaxed)) {
            if (queue.push(msg)) {
                count.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    // Consumer thread
    std::thread consumer([&]() {
        Message msg{};
        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (!stop_flag.load(std::memory_order_relaxed)) {
            if (!queue.pop(msg)) {
                std::this_thread::yield();
            }
        }
    });

    for (auto _ : state) {
        // Reset state
        count.store(0, std::memory_order_relaxed);

        // Start iteration
        start_flag.store(true, std::memory_order_release);
        auto t_start = std::chrono::steady_clock::now();

        // Measure for a fixed period
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto t_end = std::chrono::steady_clock::now();
        stop_flag.store(true, std::memory_order_release);

        double duration_sec = std::chrono::duration<double>(t_end - t_start).count();
        uint64_t ops = count.load(std::memory_order_relaxed);

        state.SetItemsProcessed(static_cast<int64_t>(ops));
        state.SetIterationTime(duration_sec);

        // Reset flags for next iteration (Google Benchmark repeats)
        stop_flag.store(false);
        start_flag.store(false);
    }

    stop_flag = true;
    producer.join();
    consumer.join();
}

// ==========================================================
// Register benchmark
// ==========================================================
BENCHMARK(BM_SPSCQueue_Throughput)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime()
    ->Iterations(5); // run few times for stability

// ==========================================================
// Main
// ==========================================================
BENCHMARK_MAIN();
