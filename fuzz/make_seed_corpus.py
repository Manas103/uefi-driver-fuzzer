"""Builds a handful of well-formed VendorConfigTable seeds for the
fuzzer's initial corpus. libFuzzer finds its own interesting inputs
from here through mutation and coverage feedback; these seeds just
give it a structurally valid starting point (right magic, right
version, at least one of every tag) instead of starting from nothing.
"""
import os
import struct


def entry(tag, payload):
    return bytes([tag]) + struct.pack("<H", len(payload)) + payload


def table(entries):
    body = bytes([ord("V"), ord("C"), 1, len(entries)])
    for e in entries:
        body += e
    checksum = 0
    for b in body:
        checksum ^= b
    return body + bytes([checksum])


def main():
    out_dir = os.path.join(os.path.dirname(__file__), "corpus")
    os.makedirs(out_dir, exist_ok=True)

    seeds = {
        "empty": table([]),
        "device_list": table([entry(1, struct.pack("<II", 0x1000, 0x2000))]),
        "string": table([entry(2, b"vendor-name")]),
        "checksum": table([entry(3, struct.pack("<I", 0xDEADBEEF))]),
        "nested": table([entry(4, bytes([1, 9, 2]) + b"hi")]),
        "vendor_ext": table([entry(5, b"\x01\x02\x03\x04")]),
        "padding": table([entry(6, b"\x00" * 4)]),
        "legacy_blob": table([entry(7, b"\xaa\xbb\xcc")]),
        "all_tags": table([
            entry(1, struct.pack("<I", 0x42)),
            entry(2, b"abc"),
            entry(3, struct.pack("<I", 0)),
            entry(4, bytes([0])),
            entry(5, b"\x01"),
            entry(6, b""),
            entry(7, b"\x01\x02"),
        ]),
    }
    for name, data in seeds.items():
        with open(os.path.join(out_dir, name), "wb") as f:
            f.write(data)
    print(f"wrote {len(seeds)} seeds to {out_dir}")


if __name__ == "__main__":
    main()
