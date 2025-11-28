#!/bin/bash

# Define the build directory here
BUILD_DIR="build/release"

# Configure
cmake . -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build "$BUILD_DIR" -j$(nproc --ignore=2)

cp $BUILD_DIR/compile_commands.json ./build/compile_commands.json

# Run
cd "$BUILD_DIR"
./App