import ctypes
from pathlib import Path

lib = ctypes.CDLL(str(Path("build/tests/descriptors.so").resolve()))
lib.usb_descriptor.argtypes = [ctypes.c_uint8, ctypes.c_uint8, ctypes.c_bool, ctypes.c_void_p]
lib.usb_descriptor.restype = ctypes.c_uint16


def descriptor(kind, index=0, hs=False):
    b = (ctypes.c_uint8 * 1024)()
    n = lib.usb_descriptor(kind, index, hs, b)
    return bytes(b[:n])


d = descriptor(1)
assert len(d) == 18 and int.from_bytes(d[8:10], "little") == 0x1209
assert int.from_bytes(d[10:12], "little") == 1 and d[12:14] == b"\x00\x02"
assert descriptor(3, 3)[2:].decode("utf-16le") == "OSD153R00001"
report = descriptor(0x22)
sizes = {}
pos = 0
report_id = 0
size = count = 0
stack = []
collections = 0
while pos < len(report):
    prefix = report[pos]
    pos += 1
    assert prefix != 0xFE, "Unexpected long HID item"
    n = [0, 1, 2, 4][prefix & 3]
    value = int.from_bytes(report[pos : pos + n], "little")
    pos += n
    typ = (prefix >> 2) & 3
    tag = prefix >> 4
    if typ == 1:
        if tag == 7:
            size = value
        elif tag == 8:
            report_id = value
            assert 1 <= value <= 255
        elif tag == 9:
            count = value
        elif tag == 10:
            stack.append((report_id, size, count))
        elif tag == 11:
            report_id, size, count = stack.pop()
    elif typ == 0:
        if tag == 10:
            collections += 1
        elif tag == 12:
            collections -= 1
            assert collections >= 0
        elif tag in (8, 9, 11):
            assert report_id and size == 8
            key = ({8: "input", 9: "output", 11: "feature"}[tag], report_id)
            sizes[key] = sizes.get(key, 0) + size * count
assert pos == len(report) and collections == 0 and not stack
assert sizes[("input", 1)] // 8 + 1 == 19
assert sizes[("output", 2)] // 8 + 1 == 1024
features = {3, 4, 125, 126, 127}
assert {i for kind, i in sizes if kind == "feature"} == features
assert all(sizes[("feature", i)] // 8 + 1 == 32 for i in features)
assert len(sizes) == 7
for hs in (False, True):
    cfg = descriptor(2, hs=hs)
    assert len(cfg) == int.from_bytes(cfg[2:4], "little") == 41
    assert cfg[4] == 1 and cfg[13] == 2 and cfg[14] == 3
    assert int.from_bytes(cfg[25:27], "little") == len(report)
    assert int.from_bytes(descriptor(0x21)[7:9], "little") == len(report)
    endpoints = []
    offset = 0
    while offset < len(cfg):
        n = cfg[offset]
        assert n >= 2 and offset + n <= len(cfg)
        if cfg[offset + 1] == 5:
            endpoints.append(
                (cfg[offset + 2], int.from_bytes(cfg[offset + 4 : offset + 6], "little"))
            )
            assert cfg[offset + 6] == 1, "interval: HS 125us microframe, FS 1ms"
        offset += n
    assert endpoints == [(0x82, 512 if hs else 64), (1, 512 if hs else 64)]
    other = descriptor(7, hs=not hs)
    assert other[1] == 7 and other[2:] == cfg[2:]
assert descriptor(0x22, 1) == b"" and descriptor(3, 9) == b""
assert descriptor(3, 1)[2:].decode("utf-16le") == "Open Stream Deck"
print("OSD HID descriptor tests passed: 1209:0001, Input19 Output1024 Feature3/7E")
