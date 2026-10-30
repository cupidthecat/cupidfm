"""Small Copy archive builder for precise malformed-header regression cases."""
import struct
import zlib

def number(value):
    for count in range(8):
        if value < 1 << (7 * (count + 1)):
            return bytes([((255 << (8 - count)) & 255) | (value >> (8 * count))]) + value.to_bytes(8, "little")[:count]
    return b"\xff" + value.to_bytes(8, "little")

def archive(*, lengths=(5,), crc_delta=0, stream_counts=(2, 1), names=("alpha", "dir", "empty", "beta", "gamma")):
    bodies = (b"alpha", b"beta", b"gamma")
    packed = bodies[0] + bodies[1] + bodies[2]
    streams = (b"\x06\x00\x02\x09" + number(9) + number(5) + b"\x00"
               b"\x07\x0b\x02\x00\x01\x01\x00\x01\x01\x00"
               b"\x0c" + number(9) + number(5) + b"\x00\x08\x0d" +
               b"".join(number(n) for n in stream_counts) + b"\x09" +
               b"".join(number(n) for n in lengths) + b"\x0a\x01" +
               b"".join(struct.pack("<I", zlib.crc32(body) ^ (crc_delta if i == 1 else 0)) for i, body in enumerate(bodies)) + b"\x00\x00")
    name_data = b"\x00" + b"".join((name + "\0").encode("utf-16le") for name in names)
    files = b"\x05" + number(len(names)) + b"\x0e\x01\x60\x0f\x01\x40\x11" + number(len(name_data)) + name_data + b"\x00"
    header = b"\x01\x04" + streams + files + b"\x00"
    start = struct.pack("<QQI", len(packed), len(header), zlib.crc32(header))
    return b"7z\xbc\xaf\x27\x1c\x00\x04" + struct.pack("<I", zlib.crc32(start)) + start + packed + header


def empty_archive(count, extra=b""):
    bits = bytes([255]) * ((count + 7) // 8)
    names = b"\x00" + b"".join((f"entry-{i:05d}\0").encode("utf-16le") for i in range(count))
    header = (b"\x01\x05" + number(count) + b"\x0e" + number(len(bits)) + bits +
              b"\x0f" + number(len(bits)) + bits + b"\x11" + number(len(names)) + names + extra + b"\x00\x00")
    start = struct.pack("<QQI", 0, len(header), zlib.crc32(header))
    return b"7z\xbc\xaf\x27\x1c\x00\x04" + struct.pack("<I", zlib.crc32(start)) + start + header
