#!/bin/bash
set -euo pipefail

ROOT_DIR=$(dirname "$0")/..
cd "$ROOT_DIR"

RESULTS_DIR=results
mkdir -p "$RESULTS_DIR"

for cfg in configs/*.json; do
  name=$(basename "$cfg" .json)
  echo "Running scenario $name"
  /usr/local/bin/router "$cfg" "$RESULTS_DIR"
done

echo "All scenarios completed. Results are in $RESULTS_DIR"
