#!/bin/sh
# Build and run the ltc_scan235 test suite from the project root.
set -e
cd "$(dirname "$0")/.."
make -s all
python3 tests/verify.py

