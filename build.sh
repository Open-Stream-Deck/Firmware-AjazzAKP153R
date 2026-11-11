#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

mkdir -p build/objects
python3 tools/version.py --header build/firmware_version.h
version=$(python3 tools/version.py)
artifact="build/AjazzAKP153R-v$version"

cc=${CC:-arm-none-eabi-gcc}
flags=(
    -march=armv7-m
    -mthumb
    -mfloat-abi=soft
    -Os
    -g3
    -ffreestanding
    -fno-builtin
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Werror
    -Iinclude
    -Ibuild
)

"$cc" "${flags[@]}" -c startup/startup.S -o build/objects/startup.o

for source in main platform usb_device usb_descriptors memory timebase keys boot_anim idle_game; do
    "$cc" "${flags[@]}" -std=c11 -c "src/$source.c" -o "build/objects/$source.o"
done

for source in display osd_protocol transition; do
    "$cc" "${flags[@]}" -O2 -std=c11 -c "src/$source.c" -o "build/objects/$source.o"
done

objects=(
    build/objects/startup.o
    build/objects/main.o
    build/objects/platform.o
    build/objects/usb_device.o
    build/objects/usb_descriptors.o
    build/objects/memory.o
    build/objects/timebase.o
    build/objects/keys.o
    build/objects/boot_anim.o
    build/objects/idle_game.o
    build/objects/display.o
    build/objects/osd_protocol.o
    build/objects/transition.o
)

"$cc" "${flags[@]}" -nostdlib \
    "-Wl,--build-id=none,--gc-sections,-Map=$artifact.map" \
    -T linker/application.ld "${objects[@]}" -lgcc -o "$artifact.elf"

python3 tools/image.py "$artifact.elf" --output "$artifact.bin"
