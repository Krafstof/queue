#include <benchmark/benchmark.h>
#include <thread>
#include <vector>
#include <queue>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <random>
#include <chrono>
#include <iostream>

// ============================================================
// Simple thread-safe queue for multi-producer / multi-consumer
// ============================================================
template <typename T>
class MPMCQueue {
public:
    explicit MPMCQueue(size_t capacity) : capacity_(capacity) {}

    bool push(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_) return false;
        queue_.push(item);
        cv_.notify_one();
        return true;
    }

    bool pop(T& out) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        out = queue_.front();
        queue_.pop();
        return true;
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    size_t capacity_;
};

// ============================================================
// Workload simulation
// ============================================================
struct Message {
    uint64_t id;
    std::array<uint8_t, 128> payload; // moderate message size
};

// ============================================================
// Scaling benchmark
// ============================================================
static void BM_Scaling_MPMCQueue(benchmark::State& state) {
    int num_producers = static_cast<int>(state.range(0));
    int num_consumers = static_cast<int>(state.range(1));
    const size_t capacity = 1 << 14;
    const int ops_per_thread = 200000;

    MPMCQueue<Message> queue(capacity);
    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> produced{0}, consumed{0};

    auto producer_fn = [&](int id) {
        std::mt19937_64 gen(id);
        for (int i = 0; i < ops_per_thread && !stop_flag.load(); ++i) {
            Message msg{static_cast<uint64_t>(i)};
            while (!queue.push(msg)) {
                std::this_thread::yield();
            }
            produced++;
        }
    };

    auto consumer_fn = [&]() {
        Message msg;
        while (!stop_flag.load() || consumed < produced) {
            if (queue.pop(msg)) {
                consumed++;
            } else {
                std::this_thread::yield();
            }
        }
    };

    for (auto _ : state) {
        produced = 0;
        consumed = 0;
        stop_flag = false;

        auto start_time = std::chrono::steady_clock::now();

        std::vector<std::thread> producers;
        std::vector<std::thread> consumers;

        for (int i = 0; i < num_producers; ++i)
            producers.emplace_back(producer_fn, i);

        for (int i = 0; i < num_consumers; ++i)
            consumers.emplace_back(consumer_fn);

        for (auto& p : producers) p.join();

        // Let consumers drain remaining messages
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        stop_flag = true;
        for (auto& c : consumers) c.join();

        auto end_time = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(end_time - start_time).count();

        double throughput = static_cast<double>(consumed.load()) / elapsed;

        state.counters["Producers"] = num_producers;
        state.counters["Consumers"] = num_consumers;
        state.counters["Produced"] = static_cast<double>(produced.load());
        state.counters["Consumed"] = static_cast<double>(consumed.load());
        state.counters["Throughput_msgs_per_s"] = throughput;
        state.counters["Latency_us_per_msg"] = (elapsed * 1e6) / consumed.load();
    }

    stop_flag = true;
}

// ============================================================
// Benchmark registrations
// ============================================================

// Arguments: (num_producers, num_consumers)
BENCHMARK(BM_Scaling_MPMCQueue)
    ->Args({1, 1})
    ->Args({2, 2})
    ->Args({4, 4})
    ->Args({8, 8})
    ->Iterations(3)
    ->UseRealTime();

BENCHMARK_MAIN();
