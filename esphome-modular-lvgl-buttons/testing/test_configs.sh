#!/bin/bash
set -euo pipefail

REPO_URL="https://github.com/agillis/esphome-modular-lvgl-buttons.git"
REPO_DIR="esphome-modular-lvgl-buttons"
EXAMPLE_DIR="$REPO_DIR/example_code"
PASS=0
FAIL=0
FAILED_FILES=()

# Check repo directory exists
if [ ! -d "$REPO_DIR" ]; then
  echo "ERROR: $REPO_DIR directory not found!"
  exit 1
fi

# Check example_code directory exists
if [ ! -d "$EXAMPLE_DIR" ]; then
  echo "ERROR: $EXAMPLE_DIR directory not found!"
  exit 1
fi

echo ""
echo "========================================="
echo "Testing config files from $EXAMPLE_DIR"
echo "========================================="
echo ""

for yaml_file in "$EXAMPLE_DIR"/*.yaml; do
  [ -f "$yaml_file" ] || continue
  basename=$(basename "$yaml_file")

  echo "--- Testing: $basename ---"

  # Copy to the directory containing the repo
  cp "$yaml_file" "$basename"

  # Run esphome config from here (above the repo)
  if output=$(esphome config "$basename" 2>&1); then
    echo "  PASS"
    ((PASS++)) || true
  else
    echo "  FAIL"
    echo "$output" | sed 's/^/    /'
    ((FAIL++)) || true
    FAILED_FILES+=("$basename")
  fi

  # Clean up the copied file
  rm -f "$basename"
  echo ""
done

echo "========================================="
echo "Results: $PASS passed, $FAIL failed"
echo "========================================="
if [ ${#FAILED_FILES[@]} -gt 0 ]; then
  echo "Failed files:"
  for f in "${FAILED_FILES[@]}"; do
    echo "  - $f"
  done
  exit 1
fi
