#!/usr/bin/env python3
import struct

SECTOR_SIZE = 512
MAGIC = b"WOFS"
VERSION = 1
SUPERBLOCK_LBA = 128
DIR_LBA = SUPERBLOCK_LBA + 1
DATA_LBA = DIR_LBA + 1

files = [
    ("hello.txt", b"Hello from WoOS VFS!\n"),
    ("readme.txt", b"WoOS minimal read-only filesystem image.\n"),
]

# superblock: magic[4], version(u16), entry_count(u16), dir_lba(u32)
superblock = bytearray(SECTOR_SIZE)
struct.pack_into("<4sHHI", superblock, 0, MAGIC, VERSION, len(files), DIR_LBA)

# dir entries: name[24], first_lba(u32), size_bytes(u32)
dir_sector = bytearray(SECTOR_SIZE)
data = bytearray()
current_lba = DATA_LBA

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
