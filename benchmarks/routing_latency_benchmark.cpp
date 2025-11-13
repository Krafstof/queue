#include <benchmark/benchmark.h>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <random>

// Simulated message
struct Message {
    int id;
    std::string payload;
};

// === Simulated router with routing overhead ===
class Router {
public:
    void routeMessage(const Message& msg) {
        // Simulate small routing overhead
        std::unique_lock<std::mutex> lock(mutex_);
        routed_messages_.push(msg);
        lock.unlock();
        cv_.notify_one();
    }

    Message getNextMessage() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]{ return !routed_messages_.empty(); });
        Message msg = routed_messages_.front();
        routed_messages_.pop();
        return msg;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Message> routed_messages_;
};

// === Direct queue (no routing logic) ===
class DirectQueue {
public:
    void push(const Message& msg) {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push(msg);
        lock.unlock();
        cv_.notify_one();
    }

    Message pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]{ return !queue_.empty(); });
        Message msg = queue_.front();
        queue_.pop();
        return msg;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Message> queue_;
};

// === Benchmark Setup ===

static void BM_RoutingLogicOverhead(benchmark::State& state) {
    Router router;
    std::atomic<bool> running{true};
    Message msg{42, "payload"};

    std::thread worker([&] {
        while (running) {
            router.getNextMessage();
        }
    });

    for (auto _ : state) {
        router.routeMessage(msg);
    }

    running = false;
    router.routeMessage(msg); // unblock thread
    worker.join();
}

static void BM_DirectQueueAccess(benchmark::State& state) {
    DirectQueue queue;
    std::atomic<bool> running{true};
    Message msg{42, "payload"};

    std::thread worker([&] {
        while (running) {
            queue.pop();
        }
    });

    for (auto _ : state) {
        queue.push(msg);
    }

    running = false;
    queue.push(msg); // unblock thread
    worker.join();
}

// === Register Benchmarks ===
BENCHMARK(BM_RoutingLogicOverhead);
BENCHMARK(BM_DirectQueueAccess);

BENCHMARK_MAIN();
