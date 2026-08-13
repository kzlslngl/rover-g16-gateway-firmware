import socket
import struct
import sys
import zlib


HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.2.166"
PORT = 502
REGISTER_BASE = 320
REGISTER_COUNT = 64


def read_exact(stream, length):
    data = stream.read(length)
    if data is None or len(data) != length:
        raise RuntimeError(f"short response: expected {length}, got {len(data or b'')}")
    return data


with socket.create_connection((HOST, PORT), timeout=3) as connection:
    request = struct.pack(">HHHBBHH", 1, 0, 6, 1, 3,
                          REGISTER_BASE, REGISTER_COUNT)
    connection.sendall(request)
    stream = connection.makefile("rb")
    header = read_exact(stream, 9)
    if header[7] & 0x80:
        raise RuntimeError(f"Modbus exception: function=0x{header[7]:02x} code={header[8]}")
    byte_count = header[8]
    payload = read_exact(stream, byte_count)

    write_request = struct.pack(">HHHBBHH", 2, 0, 6, 1, 6,
                                REGISTER_BASE, 0)
    connection.sendall(write_request)
    write_response = read_exact(stream, 9)

registers = list(struct.unpack(">64H", payload))
crc_wire = (registers[60] << 16) | registers[61]
crc_calculated = zlib.crc32(payload[:120]) & 0xFFFFFFFF
begin_sequence = (registers[4] << 16) | registers[5]
end_sequence = (registers[62] << 16) | registers[63]

assert header[0:2] == b"\x00\x01"
assert header[2:4] == b"\x00\x00"
assert header[6:9] == bytes((1, 3, 128))
assert registers[0] == 0x4547
assert registers[3] == REGISTER_COUNT
assert begin_sequence == end_sequence
assert crc_wire == crc_calculated
assert write_response == bytes((0, 2, 0, 0, 0, 3, 1, 0x86, 1))

print(f"target={HOST}:{PORT} registers={len(registers)}")
print(f"magic=0x{registers[0]:04x} version={registers[1]}.{registers[2]}")
print(f"sequence={begin_sequence} session=0x{((registers[6] << 16) | registers[7]):08x}")
print(f"heartbeat={(registers[8] << 16) | registers[9]} sbus_frames={(registers[10] << 16) | registers[11]}")
print(f"age_ms={registers[14]} flags=0x{registers[15]:04x}")
print(f"channels={registers[18:34]}")
print(f"crc=0x{crc_wire:08x} begin_end_match=yes crc_match=yes")
print("write_fc06=rejected exception=illegal_function")
