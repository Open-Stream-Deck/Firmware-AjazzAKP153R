import argparse
import re
from pathlib import Path

FIRMWARE_VERSION = "1.0.0"


def firmware_version():
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", FIRMWARE_VERSION):
        raise ValueError("FIRMWARE_VERSION must have the form MAJOR.MINOR.PATCH (for example 1.0.1)")
    if any(int(part) > 255 for part in FIRMWARE_VERSION.split(".")):
        raise ValueError("Firmware version components must fit in one byte")
    return FIRMWARE_VERSION


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Read the firmware version or generate its C header")
    parser.add_argument("--header", type=Path)
    args = parser.parse_args()
    try:
        version = firmware_version()
        if args.header:
            lines = ["#ifndef FIRMWARE_VERSION_H", "#define FIRMWARE_VERSION_H", ""]
            for name, value in zip(("MAJOR", "MINOR", "PATCH"), version.split(".")):
                lines.append(f"#define FIRMWARE_VERSION_{name} {value}u")
            lines.extend(["", "#endif", ""])
            args.header.write_text("\n".join(lines), encoding="utf-8")
        else:
            print(version)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
