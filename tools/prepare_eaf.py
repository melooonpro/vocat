#!/usr/bin/env python3
"""Validate the page0 asset and omit converter metadata from playback.

esp_lv_eaf_player 0.3.0 counts _C metadata records as animation frames but
cannot render them. Rebuild the frame table without those records, keeping
each image payload and repeated-frame reference intact.
"""

import argparse
from pathlib import Path
import struct


def prepare_eaf(data: bytes) -> bytes:
    if len(data) < 16 or data[:4] != b"\x89EAF":
        raise ValueError("Invalid EAF header")
    count, checksum, length = struct.unpack_from("<III", data, 4)
    if not count or length != len(data) - 16 or count * 8 > length:
        raise ValueError("Invalid EAF length or frame count")
    if sum(data[16:]) & 0xFFFFFFFF != checksum:
        raise ValueError("Invalid EAF checksum")

    base = 16 + count * 8
    table = bytearray()
    payload = bytearray()
    offsets = {}
    for index in range(count):
        size, offset = struct.unpack_from("<II", data, 16 + index * 8)
        if size < 4 or offset + size > len(data) - base:
            raise ValueError(f"Frame {index}: invalid bounds")
        frame = data[base + offset:base + offset + size]
        if frame[:2] != b"ZZ":
            raise ValueError(f"Frame {index}: invalid magic")
        if frame[2:4] == b"_C":
            continue
        if frame[2:4] != b"_S" or len(frame) < 20:
            raise ValueError(f"Frame {index}: unsupported image format")
        depth = frame[11]
        width, height, blocks, block_height = struct.unpack_from("<HHHH", frame, 12)
        if (width, height) != (360, 360) or depth not in (4, 8, 24):
            raise ValueError(f"Frame {index}: expected a 360x360 image with supported depth")
        palette_size = 0 if depth == 24 else (1 << depth) * 4
        header_size = 20 + blocks * 4 + palette_size
        if (not blocks or not block_height or
                blocks != (height + block_height - 1) // block_height or header_size > size):
            raise ValueError(f"Frame {index}: invalid block layout")
        block_sizes = struct.unpack_from(f"<{blocks}I", frame, 20)
        if header_size + sum(block_sizes) != size:
            raise ValueError(f"Frame {index}: invalid block lengths")

        key = (offset, size)
        if key not in offsets:
            offsets[key] = len(payload)
            payload.extend(frame)
        table.extend(struct.pack("<II", size, offsets[key]))

    if not table:
        raise ValueError("EAF contains no image frames")
    body = table + payload
    return b"\x89EAF" + struct.pack("<III", len(table) // 8,
                                    sum(body) & 0xFFFFFFFF, len(body)) + body


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = prepare_eaf(args.source.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    print(f"EAF ready: {struct.unpack_from('<I', result, 4)[0]} image frames, {len(result)} bytes")


if __name__ == "__main__":
    main()
