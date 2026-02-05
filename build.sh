#!/bin/bash
set -e
echo "Building for Quantum X Linux Environment (C++17 + O3 + AVX2)..."

g++ -O3 -march=native -Wall -shared -std=c++17 -fPIC \
    $(python3 -m pybind11 --includes) \
    engine.cpp \
    -o nano_engine$(python3-config --extension-suffix)

echo "Build successful."
