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
parser.add_argument(
    "--base-lba",
    type=int,
    default=0,
    help="Reserved for compatibility. WOFS image always uses relative LBA addressing.",
)
args = parser.parse_args()

# В образе WOFS LBA всегда относительные:
#   0 = superblock
#   1 = directory sector
#   2.. = file data
if args.base_lba != 0:
    print("warning: --base-lba ignored, WOFS now stores relative LBAs only")

superblock_lba = 0
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

    # Выравниваем каждый файл до границы сектора, иначе LBA сломаются.
    sectors = (len(payload) + SECTOR_SIZE - 1) // SECTOR_SIZE
    offset = index * 32
    name_buf = name.encode("ascii") + b"\x00"
    name_buf = name_buf.ljust(24, b"\x00")
    dir_sector[offset : offset + 24] = name_buf
    struct.pack_into("<II", dir_sector, offset + 24, current_lba, len(payload))

    data.extend(payload)
    padding = (SECTOR_SIZE - (len(payload) % SECTOR_SIZE)) % SECTOR_SIZE
    data.extend(b"\x00" * padding)
    current_lba += sectors

with open("woosfs.bin", "wb") as f:
    f.write(superblock)
    f.write(dir_sector)
    f.write(data)
