# ===== BUILD STAGE =====
FROM ubuntu:22.04 AS build
LABEL stage="build"
ARG DEBIAN_FRONTEND=noninteractive

# Install build tools
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        clang g++ make cmake build-essential git ca-certificates curl && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy project source
COPY . .

# Download nlohmann/json
RUN mkdir -p include && \
    curl -L -o include/json.hpp https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp

# === Build Google Benchmark ===
RUN git clone https://github.com/google/benchmark.git /tmp/benchmark && \
    cd /tmp/benchmark && git checkout v1.8.4 && \
    git clone https://github.com/google/googletest.git /tmp/benchmark/googletest && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_ENABLE_GTEST_TESTS=OFF .. && \
    make -j$(nproc) && make install

# === Build router binary ===
RUN clang++ -O3 -march=native -std=c++20 -pthread -Iinclude src/main.cpp -o router

# === Build queue benchmark binary (optional prebuild) ===
RUN clang++ -O3 -march=native -std=c++20 -pthread -Iinclude -I/usr/local/include \
    benchmarks/queue_benchmark.cpp -lbenchmark -lpthread -o benchmarks/queue_benchmark


# ===== RUNTIME STAGE =====
FROM ubuntu:22.04 AS runtime
LABEL stage="runtime"
ARG DEBIAN_FRONTEND=noninteractive

# Install minimal runtime deps + compiler for building benchmarks at runtime
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates bash clang g++ make && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# ✅ Copy Google Benchmark (headers and libs) from build stage
COPY --from=build /usr/local/include/benchmark /usr/local/include/benchmark
COPY --from=build /usr/local/lib /usr/local/lib

# Copy binaries and project assets
COPY --from=build /app/router /usr/local/bin/router
COPY --from=build /app/benchmarks/queue_benchmark /usr/local/bin/queue_benchmark
COPY configs configs
COPY scripts scripts

# Add helper run_test script
RUN printf '#!/bin/bash\nset -e\nCONFIG_NAME=${1:-baseline.json}\nRESULTS_DIR=${2:-results}\nCONFIG="configs/${CONFIG_NAME}"\n[ ! -f "$CONFIG" ] && echo "Error: $CONFIG not found." && exit 1\nmkdir -p "$RESULTS_DIR"\n/usr/local/bin/router "$CONFIG" "$RESULTS_DIR"\n' \
    > /usr/local/bin/run_test && chmod +x /usr/local/bin/run_test

# Create symlink in /app so ./run_test works naturally
RUN ln -s /usr/local/bin/run_test /app/run_test

# Ensure benchmark/test scripts are executable
RUN chmod +x scripts/run_all_tests.sh scripts/run_benchmarks.sh

# Default entrypoint
CMD ["/bin/bash", "-c", "echo 'Use docker compose to run tests or benchmarks.'"]
