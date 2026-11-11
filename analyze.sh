#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

mkdir -p build/analysis
python3 tools/version.py --header build/firmware_version.h

cc=${CC:-arm-none-eabi-gcc}
flags=(
    -march=armv7-m
    -mthumb
    -mfloat-abi=soft
    -std=c11
    -ffreestanding
    -fno-builtin
    -Wall
    -Wextra
    -Werror
    -fanalyzer
    -Iinclude
    -Ibuild
)

for source in src/*.c; do
    optimization=-Os
    case "$source" in
        src/display.c|src/osd_protocol.c|src/transition.c) optimization=-O2 ;;
    esac
    "$cc" "${flags[@]}" "$optimization" -c "$source" \
        -o "build/analysis/$(basename "$source" .c).o"
done
