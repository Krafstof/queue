Описание:
https://www.notion.so/Multi-Stage-Lock-Free-Message-Router-24c847a1e0f480c382addb65a0f91d58

Примеры команд для запуска:
# Build the project
docker-compose build

# Run all test scenarios
docker-compose run router-test

# Run specific scenario
docker-compose run router-test ./run_test baseline.json

# Run benchmarks
docker-compose run router-benchmark

# Run specific benchmark
docker-compose run router-benchmark queue_perf
