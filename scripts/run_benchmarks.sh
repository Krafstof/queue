#!/bin/bash
set -euo pipefail

ROOT_DIR=$(dirname "$0")/..
cd "$ROOT_DIR"

RESULT_DIR=results/benchmarks
mkdir -p "$RESULT_DIR"

TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Optional first argument: name of benchmark to run
BENCH_TO_RUN=${1:-all}

echo "🔧 Building and running benchmarks..."

for SRC in benchmarks/*.cpp; do
    NAME=$(basename "$SRC" .cpp)
    
    # Skip if we want only a specific benchmark
    if [[ "$BENCH_TO_RUN" != "all" && "$BENCH_TO_RUN" != "$NAME" ]]; then
        continue
    fi

    BIN=benchmarks/bin/$NAME

    echo "  → Building $NAME..."
    mkdir -p benchmarks/bin
    if [ ! -f "$BIN" ] || [ "$SRC" -nt "$BIN" ]; then
        if command -v clang++ >/dev/null 2>&1; then
            clang++ -O3 -march=native -std=c++20 -pthread -Iinclude -I/usr/local/include "$SRC" -lbenchmark -lpthread -o "$BIN"
        elif command -v g++ >/dev/null 2>&1; then
            g++ -O3 -march=native -std=c++20 -pthread -Iinclude -I/usr/local/include "$SRC" -lbenchmark -lpthread -o "$BIN"
        else
            echo "❌ No C++ compiler found."
            exit 1
        fi
    fi

    OUT_FILE="$RESULT_DIR/${NAME}_${TIMESTAMP}.txt"
    echo "  → Running $NAME, results in $OUT_FILE"
    "$BIN" | tee "$OUT_FILE"
done

echo "✅ Benchmarks completed."
