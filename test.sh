#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

mkdir -p build/tests
python3 tools/version.py --header build/firmware_version.h

cc=${HOST_CC:-gcc}
flags=(
    -std=c11
    -Wall
    -Wextra
    -Werror
    -fsanitize=address,undefined
    -fno-sanitize-recover=all
    -g
    -Iinclude
    -Ibuild
)

"$cc" "${flags[@]}" -O1 -ffunction-sections -fdata-sections -Wl,--gc-sections \
    tests/main_test.c -o build/tests/main_test
./build/tests/main_test

"$cc" "${flags[@]}" src/osd_protocol.c tests/protocol_test.c -o build/tests/protocol_test
./build/tests/protocol_test

"$cc" "${flags[@]}" -DUSB_HOST_TEST \
    src/usb_device.c src/usb_descriptors.c tests/usb_control_test.c \
    -o build/tests/usb_control_test
./build/tests/usb_control_test

"$cc" "${flags[@]}" -DKEYS_HOST_TEST \
    src/keys.c tests/keys_test.c -o build/tests/keys_test
./build/tests/keys_test

"$cc" "${flags[@]}" -DTIMEBASE_HOST_TEST \
    src/timebase.c tests/timebase_test.c -o build/tests/timebase_test
./build/tests/timebase_test

"$cc" "${flags[@]}" -DDISPLAY_HOST_TEST \
    src/display.c tests/display_test.c -o build/tests/display_test
./build/tests/display_test

"$cc" -std=c11 -Wall -Wextra -Werror -Iinclude -shared -fPIC \
    src/usb_descriptors.c -o build/tests/descriptors.so
"$cc" "${flags[@]}" src/boot_anim.c src/idle_game.c tests/boot_anim_test.c -o build/tests/boot_anim_test
./build/tests/boot_anim_test

"$cc" "${flags[@]}" src/transition.c tests/transition_test.c -o build/tests/transition_test
./build/tests/transition_test

"$cc" "${flags[@]}" tests/idle_game_test.c -o build/tests/idle_game_test
./build/tests/idle_game_test

python3 tests/descriptor_test.py
python3 tests/image_test.py
python3 tests/version_test.py
