#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <algorithm>
#include <memory>
#include <string>
#include <iomanip>
#include <filesystem>
#include <cstdint>
#include <array>
#include <mutex>
#include "../include/json.hpp"

using json = nlohmann::json;

// ==========================================================
// Lock-Free Single Producer Single Consumer Queue
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

    size_t size() const {
        auto head = head_.load(std::memory_order_relaxed);
        auto tail = tail_.load(std::memory_order_relaxed);
        return (head + Capacity - tail) % Capacity;
    }

private:
    std::array<T, Capacity> buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

// ==========================================================
// Message Structure
// ==========================================================
struct Message {
    uint8_t msg_type;
    uint8_t producer_id;
    uint32_t sequence;
    uint64_t timestamp_ns;
    uint8_t processor_id;
    uint64_t processed_ns;
};

// ==========================================================
// Config Parsing
// ==========================================================
struct Config {
    int duration_secs;
    int producer_count;
    int processor_count;
    int strategy_count;
    std::vector<int> stage1_routing;
    std::vector<int> stage2_routing;
};

Config load_config(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot open config file: " + path);

    json j;
    f >> j;

    Config cfg;
    cfg.duration_secs = j["duration_secs"];
    cfg.producer_count = j["producers"]["count"];
    cfg.processor_count = j["processors"]["count"];
    cfg.strategy_count = j["strategies"]["count"];

    cfg.stage1_routing.resize(8, 0);
    for (auto& rule : j["stage1_rules"]) {
        int type = rule["msg_type"];
        cfg.stage1_routing[type] = rule["processors"][0];
    }

    cfg.stage2_routing.resize(8, 0);
    for (auto& rule : j["stage2_rules"]) {
        int type = rule["msg_type"];
        cfg.stage2_routing[type] = rule["strategy"];
    }

    return cfg;
}

static inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

constexpr size_t QUEUE_SIZE = 1 << 14;

// ==========================================================
// Latency Statistics
// ==========================================================
struct LatencyStats {
    std::mutex mtx;
    std::vector<double> stage1_us;
    std::vector<double> processing_us;
    std::vector<double> stage2_us;
    std::vector<double> total_us;

    void add(double s1, double proc, double s2, double total) {
        std::lock_guard<std::mutex> lock(mtx);
        stage1_us.push_back(s1);
        processing_us.push_back(proc);
        stage2_us.push_back(s2);
        total_us.push_back(total);
    }

    static double percentile(std::vector<double>& v, double p) {
        if (v.empty()) return 0.0;
        std::sort(v.begin(), v.end());
        size_t idx = std::min((size_t)(p * v.size()), v.size() - 1);
        return v[idx];
    }
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <config.json> <results_dir>\n";
        return 1;
    }

    std::string config_path = argv[1];
    std::string results_dir = argv[2];
    std::filesystem::create_directories(results_dir);

    std::string scenario = std::filesystem::path(config_path).stem().string();
    std::string log_path = results_dir + "/" + scenario + "_log.txt";
    std::string summary_path = results_dir + "/" + scenario + "_summary.txt";
    std::ofstream log_file(log_path);
    std::ofstream summary_file(summary_path);

    Config cfg = load_config(config_path);
    std::cout << "Running scenario: " << scenario << std::endl;

    std::vector<std::unique_ptr<SPSCQueue<Message, QUEUE_SIZE>>> stage1_queues;
    std::vector<std::unique_ptr<SPSCQueue<Message, QUEUE_SIZE>>> stage2_queues;
    for (int i = 0; i < cfg.processor_count; ++i)
        stage1_queues.push_back(std::make_unique<SPSCQueue<Message, QUEUE_SIZE>>());
    for (int i = 0; i < cfg.strategy_count; ++i)
        stage2_queues.push_back(std::make_unique<SPSCQueue<Message, QUEUE_SIZE>>());

    std::atomic<bool> stop_flag = false;
    std::atomic<uint64_t> produced = 0, processed = 0, delivered = 0;
    LatencyStats latencies;

    // ==========================================================
    // Producers
    // ==========================================================
    std::vector<std::thread> producers;
    for (int pid = 0; pid < cfg.producer_count; ++pid) {
        producers.emplace_back([&, pid]() {
            uint32_t seq = 0;
            std::mt19937 gen(pid + 1);
            std::uniform_int_distribution<int> type_dist(0, 3);
            while (!stop_flag.load(std::memory_order_relaxed)) {
                Message msg;
                msg.msg_type = type_dist(gen);
                msg.producer_id = pid;
                msg.sequence = seq++;
                msg.timestamp_ns = now_ns();

                int proc_id = cfg.stage1_routing[msg.msg_type];
                auto& q = *stage1_queues[proc_id];
                if (q.push(msg)) {
                    produced++;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    // ==========================================================
    // Processors
    // ==========================================================
    std::vector<std::thread> processors;
    for (int proc_id = 0; proc_id < cfg.processor_count; ++proc_id) {
        processors.emplace_back([&, proc_id]() {
            Message msg;
            while (!stop_flag.load(std::memory_order_relaxed)) {
                if (stage1_queues[proc_id]->pop(msg)) {
                    uint64_t t_now = now_ns();
                    double stage1_us = (t_now - msg.timestamp_ns) / 1000.0;
                    msg.processor_id = proc_id;
                    msg.processed_ns = t_now;
                    double processing_us = 0.0; // negligible in this simple demo

                    int strat_id = cfg.stage2_routing[msg.msg_type];
                    while (!stage2_queues[strat_id]->push(msg)) {
                        if (stop_flag.load()) return;
                        std::this_thread::yield();
                    }
                    processed++;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    // ==========================================================
    // Strategies
    // ==========================================================
    std::vector<std::thread> strategies;
    for (int sid = 0; sid < cfg.strategy_count; ++sid) {
        strategies.emplace_back([&, sid]() {
            Message msg;
            while (!stop_flag.load(std::memory_order_relaxed)) {
                if (stage2_queues[sid]->pop(msg)) {
                    uint64_t t_end = now_ns();
                    double stage2_us = (t_end - msg.processed_ns) / 1000.0;
                    double stage1_us = (msg.processed_ns - msg.timestamp_ns) / 1000.0;
                    double processing_us = stage2_us; // simple proxy
                    double total_us = (t_end - msg.timestamp_ns) / 1000.0;

                    latencies.add(stage1_us, processing_us, stage2_us, total_us);
                    delivered++;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    // ==========================================================
    // Monitoring loop
    // ==========================================================
    auto start = std::chrono::steady_clock::now();
    uint64_t prev_prod = 0, prev_proc = 0, prev_del = 0;

    for (int sec = 1; sec <= cfg.duration_secs; ++sec) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        uint64_t p = produced.load(), r = processed.load(), d = delivered.load();

        double produced_m = (p - prev_prod) / 1e6;
        double processed_m = (r - prev_proc) / 1e6;
        double delivered_m = (d - prev_del) / 1e6;

        uint64_t lost_now = (p - d) - (prev_prod - prev_del);
        double lost_m = lost_now / 1e6;

        prev_prod = p;
        prev_proc = r;
        prev_del = d;

        // Queue sizes
        std::ostringstream s1, s2;
        s1 << "[";
        for (size_t i = 0; i < stage1_queues.size(); ++i) {
            s1 << stage1_queues[i]->size();
            if (i + 1 < stage1_queues.size()) s1 << ", ";
        }
        s1 << "]";
        s2 << "[";
        for (size_t i = 0; i < stage2_queues.size(); ++i) {
            s2 << stage2_queues[i]->size();
            if (i + 1 < stage2_queues.size()) s2 << ", ";
        }
        s2 << "]";

        std::ostringstream line;
        line << "[" << std::fixed << std::setprecision(2) << (double)sec
             << "s] Produced: " << produced_m << "M | "
             << "Processed: " << processed_m << "M | "
             << "Delivered: " << delivered_m << "M | "
             << "Lost: " << lost_m << "M | "
             << "Stage1 Queues: " << s1.str()
             << " | Stage2 Queues: " << s2.str();

        std::cout << line.str() << std::endl;
        log_file << line.str() << "\n";
    }

    stop_flag = true;

    for (auto& t : producers) t.join();
    for (auto& t : processors) t.join();
    for (auto& t : strategies) t.join();

    // ==========================================================
    // Summary
    // ==========================================================
    summary_file << "=== PERFORMANCE SUMMARY ===\n";
    summary_file << "Scenario: " << scenario << "\n";
    summary_file << "Duration: " << cfg.duration_secs << " seconds\n";
    summary_file << "Produced:  " << produced << "\n";
    summary_file << "Processed: " << processed << "\n";
    summary_file << "Delivered: " << delivered << "\n";

    summary_file << "\nLatency Percentiles (μs):\n";
    summary_file << "Stage      p50    p90    p99\n";

    summary_file << "Stage1   "
                 << LatencyStats::percentile(latencies.stage1_us, 0.50) << "  "
                 << LatencyStats::percentile(latencies.stage1_us, 0.90) << "  "
                 << LatencyStats::percentile(latencies.stage1_us, 0.99) << "\n";

    summary_file << "Process  "
                 << LatencyStats::percentile(latencies.processing_us, 0.50) << "  "
                 << LatencyStats::percentile(latencies.processing_us, 0.90) << "  "
                 << LatencyStats::percentile(latencies.processing_us, 0.99) << "\n";

    summary_file << "Stage2   "
                 << LatencyStats::percentile(latencies.stage2_us, 0.50) << "  "
                 << LatencyStats::percentile(latencies.stage2_us, 0.90) << "  "
                 << LatencyStats::percentile(latencies.stage2_us, 0.99) << "\n";

    summary_file << "Total    "
                 << LatencyStats::percentile(latencies.total_us, 0.50) << "  "
                 << LatencyStats::percentile(latencies.total_us, 0.90) << "  "
                 << LatencyStats::percentile(latencies.total_us, 0.99) << "\n";

    std::cout << "Scenario " << scenario << " complete. Results written to "
              << summary_path << std::endl;

    return 0;
}
