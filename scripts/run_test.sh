#!/bin/bash
set -euo pipefail

# Take the first argument as config filename only (without path)
CONFIG_NAME=${1:-baseline.json}
RESULTS_DIR=${2:-results}

# Prepend configs/ folder automatically
CONFIG="configs/${CONFIG_NAME}"

# Check if the config file exists
if [ ! -f "$CONFIG" ]; then
    echo "Error: config file '$CONFIG' does not exist."
    exit 1
fi

echo "Running scenario from: $CONFIG"

# Ensure results directory exists
mkdir -p "$RESULTS_DIR"

# Run the router binary
/usr/local/bin/router "$CONFIG" "$RESULTS_DIR"

echo "Test completed. Results in $RESULTS_DIR"
#!/bin/bash
set -euo pipefail

# --- Config filename (without path) ---
CONFIG_NAME=${1:-baseline.json}
RESULTS_DIR=${2:-results}

# --- Full path inside container ---
CONFIG="/app/configs/${CONFIG_NAME}"

# --- Check that the config file exists ---
if [ ! -f "$CONFIG" ]; then
    echo "Error: config file '$CONFIG' does not exist in /app/configs."
    exit 1
fi

echo "Running scenario from: $CONFIG"

# --- Ensure results directory exists ---
mkdir -p "$RESULTS_DIR"

# --- Run the router binary ---
/usr/local/bin/router "$CONFIG" "$RESULTS_DIR"

echo "Test completed. Results in $RESULTS_DIR"
