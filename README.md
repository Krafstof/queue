# Multi‑Stage Lock‑Free Message Router

This project implements a simplified version of a high‑performance message routing system.  It is inspired by a technical assignment that requires routing millions of messages per second through multiple processing stages without using locks.  The goal is to preserve ordering of messages from the same producer/type, achieve high throughput and low latency, and provide a reproducible Docker environment for testing and benchmarking.

## Project Structure

```
project/
├── Dockerfile              # Multi‑stage build for router and benchmark
├── docker-compose.yml      # Orchestrates all tests and benchmarks
├── configs/               # Configuration files for the six test scenarios
├── src/                   # Source code for the router
├── benchmarks/            # Benchmark code for the lock‑free queue
├── scripts/               # Helper scripts to run tests and benchmarks
└── results/               # Output directory for logs and summaries
```

### Configurations

The `configs/` directory contains JSON‑like files describing six scenarios:

* **baseline.json** – four producers send an even distribution of four message types at one million messages per second for ten seconds.  Each type is mapped to a dedicated processor and strategy.
* **hot_type.json** – similar to baseline but 70 % of messages are of type 0, creating an imbalanced hot spot.
* **burst_pattern.json** – producers alternate between a 200 ms burst at 5× normal rate and a quiet period of 1800 ms at 0.5× normal rate.
* **imbalanced_processing.json** – processing times per type vary widely (50 ns to 2000 ns) to exercise backpressure handling.
* **ordering_stress.json** – all producers send only type 0 messages; there is a single processor and strategy to test extreme contention on ordering.
* **strategy_bottleneck.json** – strategy 0 processes slowly while strategies 1–2 process quickly, creating a bottleneck on one consumer.

You can add additional scenarios by creating similar JSON files.

### Building and Running

This repository expects a C++20 compiler (clang or g++) and GNU Make.  On a Linux host with a compiler installed you can build and run directly:

```bash
./scripts/run_all_tests.sh
./scripts/run_benchmarks.sh 1000000
```

All output goes to the `results/` directory.  Each scenario produces a log file with per‑second statistics and a summary file with latency percentiles and ordering validation.

#### Docker

If you do not have a compiler or wish to run in a reproducible environment, use Docker:

```bash
docker compose build
docker compose run router-test
docker compose run router-benchmark
```

The Dockerfile performs a multi‑stage build: it installs `clang` in the build stage, compiles the router and benchmark, and copies the binaries into a minimal runtime image.  Docker Compose mounts the `results/` directory so that logs and summaries are available on the host.

### Design Notes

* **Lock‑free queues** –  The router uses a multi‑producer single‑consumer (MPSC) ring buffer for the queues between producers/processors and processors/strategies.  Producers reserve slots via an atomic fetch‑add; the consumer reads via an atomic index.  When the queue is full producers busy‑wait until space is available.
* **Thread per role** –  Each producer, processor and strategy runs on its own thread.  No mutexes or semaphores are used.  Busy‑waiting avoids scheduling overhead and yields the lowest latency but assumes CPU resources are available.
* **Configuration parsing** –  The program does not depend on external JSON libraries.  It uses basic regular expressions to extract values from the configuration files.  Unknown fields are ignored.
* **Real‑time metrics** –  A monitoring thread prints per‑second statistics: numbers of produced, processed and delivered messages, queue depths and average latencies.  Final percentiles (p50, p90, p99, p99.9, max) are computed from the recorded latencies at the end of each run.
* **Test scripts** –  `scripts/run_all_tests.sh` builds the router if needed and executes it for every configuration file in `configs/`.  The results are written to `results/<scenario>_log.txt` and `results/<scenario>_summary.txt`.
* **Benchmark** –  The `benchmarks/queue_benchmark.cpp` program measures the throughput of the MPSC queue in isolation.  `scripts/run_benchmarks.sh` builds and runs this benchmark.

### Limitations

This implementation is a functional demonstration rather than a tuned high‑performance solution.  It should compile and run on systems with a C++17/20 compiler but has not been optimized for millions of messages per second as required by the original assignment.  Feel free to use it as a starting point and refine the data structures, batching strategies and memory management to meet stringent latency and throughput targets.