#!/bin/bash
cmake . -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
