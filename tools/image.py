import argparse
import hashlib
import json
import struct
from pathlib import Path

from version import firmware_version

APPLICATION_BASE = 0x08004000
APPLICATION_BYTES = 96496
RAM_BASE = 0x20000000
RAM_END = 0x20020000
VECTOR_WORDS = 80


def require(condition, message):
    if not condition:
        raise ValueError(message)


def crc16_arc(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def read_image(path):
    data = path.read_bytes()
    require(len(data) >= 52, "Truncated ELF header")
    require(data[:7] == b"\x7fELF\x01\x01\x01", "Expected ELF32 little endian")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    machine, entry, offset, stride, count = header[2], header[4], header[5], header[9], header[10]
    require(machine == 40, "Expected ARM ELF")
    require(stride == 32, "Invalid program header size")
    require(offset + stride * count <= len(data), "Truncated program headers")

    segments = []
    for index in range(count):
        kind, start, virtual, physical, size, memory_size, _, _ = struct.unpack_from(
            "<8I", data, offset + index * stride
        )
        if kind != 1:
            continue
        require(start + size <= len(data), "Truncated load segment")
        require(size <= memory_size, "Invalid load segment size")
        if size:
            require(
                APPLICATION_BASE
                <= physical
                < physical + size
                <= APPLICATION_BASE + APPLICATION_BYTES,
                "Load segment exceeds the application region",
            )
            segments.append((physical, data[start : start + size]))
        if RAM_BASE <= virtual < 0x30000000:
            require(virtual + memory_size <= RAM_END, "Load segment exceeds the RAM region")

    require(bool(segments), "No application load segments")
    segments.sort()
    end = max(address + len(content) for address, content in segments)
    image = bytearray(b"\xff" * (end - APPLICATION_BASE))
    previous_end = APPLICATION_BASE
    for address, content in segments:
        require(address >= previous_end, "Overlapping load segments")
        image[address - APPLICATION_BASE : address - APPLICATION_BASE + len(content)] = content
        previous_end = address + len(content)

    require(VECTOR_WORDS * 4 <= len(image) <= APPLICATION_BYTES, "Invalid application size")
    require(len(image) % 4 == 0, "Application size must be word aligned")
    vectors = struct.unpack_from(f"<{VECTOR_WORDS}I", image)
    require(vectors[0] == RAM_END, "Unexpected initial stack pointer")
    require(vectors[1] == entry and entry & 1, "Invalid reset vector")
    for index, vector in enumerate(vectors[1:], 1):
        if index in (7, 8, 9, 10, 13):
            require(vector == 0, "Reserved vector must be zero")
        else:
            require(
                vector & 1 and APPLICATION_BASE + VECTOR_WORDS * 4 <= vector & ~1 < end,
                f"Invalid vector {index}",
            )
    return bytes(image), vectors[0], entry


def main():
    parser = argparse.ArgumentParser(description="Export and verify an AKP153R application image")
    parser.add_argument("elf", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        version = firmware_version()
        image, stack, entry = read_image(args.elf)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f"{error}\n")

    output = args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    manifest = {
        "file": output.name,
        "firmware_version": version,
        "protocol": "OSD1 v1",
        "bytes": len(image),
        "words": len(image) // 4,
        "sha256": hashlib.sha256(image).hexdigest(),
        "crc16_arc": f"0x{crc16_arc(image):04X}",
        "application_base": f"0x{APPLICATION_BASE:08X}",
        "stack_top": f"0x{stack:08X}",
        "reset_thumb": f"0x{entry:08X}",
        "expected_product": "OSD AKP153R",
        "expected_vid_pid": "1209:0001",
        "expected_bcd_device": "0200",
        "expected_serial": "OSD153R00001",
    }
    text = json.dumps(manifest, indent=2) + "\n"
    output.with_name("firmware.json").write_text(text, encoding="utf-8")
    print(text, end="")


if __name__ == "__main__":
    main()
