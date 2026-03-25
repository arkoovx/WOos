#!/usr/bin/env python3
import argparse
import struct

SECTOR_SIZE = 512
MAGIC = b"WOFS"
VERSION = 1

files = [
    ("hello.txt", b"Hello from WoOS VFS!\n"),
    ("readme.txt", b"WoOS minimal read-only filesystem image.\n"),
]

parser = argparse.ArgumentParser(description="Build minimal WOFS image")
parser.add_argument("--base-lba", type=int, default=2876, help="LBA where superblock will be placed in os.img")
args = parser.parse_args()

if args.base_lba < 2:
    raise ValueError("base-lba must be >= 2")
if args.base_lba > 2876:
    raise ValueError("base-lba must be <= 2876 for 1.44MB image")

superblock_lba = args.base_lba
dir_lba = superblock_lba + 1
data_lba = dir_lba + 1

# superblock: magic[4], version(u16), entry_count(u16), dir_lba(u32)
superblock = bytearray(SECTOR_SIZE)
struct.pack_into("<4sHHI", superblock, 0, MAGIC, VERSION, len(files), dir_lba)

# dir entries: name[24], first_lba(u32), size_bytes(u32)
dir_sector = bytearray(SECTOR_SIZE)
data = bytearray()
current_lba = data_lba

for index, (name, payload) in enumerate(files):
    if len(name.encode("ascii")) > 23:
        raise ValueError(f"filename too long: {name}")

    # Выравниваем каждый файл до целого числа секторов.
    sectors = (len(payload) + SECTOR_SIZE - 1) // SECTOR_SIZE
    offset = index * 32
    name_buf = name.encode("ascii") + b"\x00"
    name_buf = name_buf.ljust(24, b"\x00")
    dir_sector[offset : offset + 24] = name_buf
    struct.pack_into("<II", dir_sector, offset + 24, current_lba, len(payload))

    padded = payload.ljust(sectors * SECTOR_SIZE, b"\x00")
    data.extend(padded)
    current_lba += sectors

with open("woosfs.bin", "wb") as f:
    f.write(superblock)
    f.write(dir_sector)
    f.write(data)
