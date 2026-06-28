#!/usr/bin/env python3
import struct

SECTOR_SIZE = 512
TOTAL_SECTORS = 1024  # 512 KB FAT12 image
RESERVED_SECTORS = 1
FAT_COUNT = 2
SECTORS_PER_FAT = 9
ROOT_DIR_ENTRIES = 224
SECTORS_PER_CLUSTER = 1

# Calculate start sectors
FAT1_START = RESERVED_SECTORS
FAT2_START = FAT1_START + SECTORS_PER_FAT
ROOT_START = FAT2_START + SECTORS_PER_FAT
ROOT_SECTORS = (ROOT_DIR_ENTRIES * 32 + SECTOR_SIZE - 1) // SECTOR_SIZE
DATA_START = ROOT_START + ROOT_SECTORS

def main():
    # 1. Create empty image
    img = bytearray(TOTAL_SECTORS * SECTOR_SIZE)

    # 2. Setup BPB (Boot Parameter Block) in sector 0
    # Jump + OEM Name
    img[0:3] = b"\xeb\x3c\x90"
    img[3:11] = b"MSWIN4.1"
    # BPB fields
    struct.pack_into("<HBHBHHBHHHII", img, 11,
        SECTOR_SIZE,          # Bytes per sector (512)
        SECTORS_PER_CLUSTER,  # Sectors per cluster (1)
        RESERVED_SECTORS,     # Reserved sectors (1)
        FAT_COUNT,            # Number of FATs (2)
        ROOT_DIR_ENTRIES,     # Root entries (224)
        TOTAL_SECTORS,        # Total sectors (1024)
        0xF8,                 # Media descriptor (0xF8 = HDD)
        SECTORS_PER_FAT,      # Sectors per FAT (9)
        18,                   # Sectors per track (18)
        2,                    # Number of heads (2)
        0,                    # Hidden sectors (0)
        0                     # Large sectors (0)
    )
    # Extended Boot Record
    img[36] = 0x80            # Drive number (0x80)
    img[37] = 0               # Reserved
    img[38] = 0x29            # Signature
    struct.pack_into("<I11s8s", img, 39,
        0x12345678,           # Volume ID
        b"WOOS       ",       # Volume label (11 bytes)
        b"FAT12   "           # FS type (8 bytes)
    )
    # Boot signature
    img[510] = 0x55
    img[511] = 0xAA

    # 3. Define files to inject
    files = [
        ("HELLO.TXT", b"Hello from WoOS VFS (FAT12)!\n"),
        ("README.TXT", b"WoOS minimal read-write FAT12 filesystem image.\n")
    ]

    # Write FAT tables (FAT1 & FAT2)
    # First 2 entries: FAT ID (0xF8) + EOF (0xFFF)
    # Byte 0 = 0xF8, Byte 1 = 0xFF, Byte 2 = 0xFF
    fat_data = bytearray(SECTORS_PER_FAT * SECTOR_SIZE)
    fat_data[0] = 0xF8
    fat_data[1] = 0xFF
    fat_data[2] = 0xFF

    # 4. Write directory entries and data clusters
    root_dir = bytearray(ROOT_SECTORS * SECTOR_SIZE)
    current_cluster = 2

    for index, (name, content) in enumerate(files):
        # Split name and ext
        parts = name.split(".")
        fname = parts[0].ljust(8, " ")[:8].encode("ascii")
        fext = parts[1].ljust(3, " ")[:3].encode("ascii")

        # Root directory entry (32 bytes)
        offset = index * 32
        root_dir[offset : offset + 8] = fname
        root_dir[offset + 8 : offset + 11] = fext
        root_dir[offset + 11] = 0x00  # Attribute (normal file)
        struct.pack_into("<HI", root_dir, offset + 26, current_cluster, len(content))

        # Write data to cluster
        data_sector = DATA_START + (current_cluster - 2) * SECTORS_PER_CLUSTER
        data_offset = data_sector * SECTOR_SIZE
        img[data_offset : data_offset + len(content)] = content

        # Update FAT entry for this cluster (set to 0xFFF = EOF)
        # Entry 2C and 2C+1 pack helper
        entry_idx = current_cluster
        byte_pos = (entry_idx * 3) // 2
        if entry_idx % 2 == 0:
            fat_data[byte_pos] = 0xFF
            fat_data[byte_pos + 1] |= 0x0F
        else:
            fat_data[byte_pos] |= 0xF0
            fat_data[byte_pos + 1] = 0xFF

        current_cluster += 1

    # Copy FAT tables to image
    img[FAT1_START * SECTOR_SIZE : (FAT1_START + SECTORS_PER_FAT) * SECTOR_SIZE] = fat_data
    img[FAT2_START * SECTOR_SIZE : (FAT2_START + SECTORS_PER_FAT) * SECTOR_SIZE] = fat_data
    # Copy Root Directory to image
    img[ROOT_START * SECTOR_SIZE : (ROOT_START + ROOT_SECTORS) * SECTOR_SIZE] = root_dir

    # 5. Write filesystem image
    with open("woosfs.bin", "wb") as f:
        f.write(img)
    print("woosfs.bin created successfully (FAT12, 512KB)")

if __name__ == "__main__":
    main()
