#!/usr/bin/env python3
import struct
import os

SECTOR_SIZE = 512
TOTAL_SECTORS = 131072       # 64 MB FAT32 image
RESERVED_SECTORS = 32        # Standard FAT32 reserved sectors (VBR, FSInfo, backups)
FAT_COUNT = 2
SECTORS_PER_FAT = 1024       # 1024 sectors * 512 bytes / 4 bytes per entry = 131072 entries
SECTORS_PER_CLUSTER = 1      # 1 sector (512 B) per cluster -> 128992 data clusters (> 65525 = FAT32)
ROOT_DIR_CLUSTERS = 4        # 4 clusters = 2048 bytes (64 directory entries initial capacity)

FAT1_START = RESERVED_SECTORS
FAT2_START = FAT1_START + SECTORS_PER_FAT
DATA_START = RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT
TOTAL_DATA_CLUSTERS = (TOTAL_SECTORS - DATA_START) // SECTORS_PER_CLUSTER

def make_entry(name, ext, attr, start_cluster, size):
    ent = bytearray(32)
    ent[0:8] = name.ljust(8, " ")[:8].encode("ascii")
    ent[8:11] = ext.ljust(3, " ")[:3].encode("ascii")
    ent[11] = attr
    struct.pack_into("<HHHHI", ent, 20,
        (start_cluster >> 16) & 0xFFFF, # DIR_FstClusHI
        0,                              # DIR_WrtTime
        0x5CE0,                         # DIR_WrtDate
        start_cluster & 0xFFFF,         # DIR_FstClusLO
        size                            # DIR_FileSize
    )
    return ent

def main():
    img = bytearray(TOTAL_SECTORS * SECTOR_SIZE)

    # 1. Setup Sector 0 (VBR / Boot Sector)
    img[0:3] = b"\xeb\x58\x90"
    img[3:11] = b"MSWIN4.1"
    struct.pack_into("<HBHBHHBHHHII", img, 11,
        SECTOR_SIZE,          # BPB_BytsPerSec (512)
        SECTORS_PER_CLUSTER,  # BPB_SecPerClus (1)
        RESERVED_SECTORS,     # BPB_RsvdSecCnt (32)
        FAT_COUNT,            # BPB_NumFATs (2)
        0,                    # BPB_RootEntCnt (0 for FAT32)
        0,                    # BPB_TotSec16 (0 for FAT32)
        0xF8,                 # BPB_Media (0xF8 = Fixed disk)
        0,                    # BPB_FATSz16 (0 for FAT32)
        63,                   # BPB_SecPerTrk
        16,                   # BPB_NumHeads
        0,                    # BPB_HiddSec
        TOTAL_SECTORS         # BPB_TotSec32 (131072)
    )
    struct.pack_into("<IHHIHH12sBBBI11s8s", img, 36,
        SECTORS_PER_FAT,      # BPB_FATSz32 (1024)
        0,                    # BPB_ExtFlags (0 = mirroring active)
        0,                    # BPB_FSVer (0:0)
        2,                    # BPB_RootClus (cluster 2)
        1,                    # BPB_FSInfo (sector 1)
        6,                    # BPB_BkBootSec (sector 6)
        b"\x00" * 12,         # Reserved
        0x80,                 # BS_DrvNum (0x80 = Hard disk)
        0,                    # BS_NTres
        0x29,                 # BS_BootSig
        0x12345678,           # BS_VolID
        b"WOOS FAT32 ",       # BS_VolLab (11 bytes)
        b"FAT32   "           # BS_FilSysType (8 bytes)
    )
    img[510] = 0x55
    img[511] = 0xAA

    # 2. Setup FAT tables
    fat_data = bytearray(SECTORS_PER_FAT * SECTOR_SIZE)
    # Cluster 0 and 1: media signature and end-of-chain marker
    struct.pack_into("<II", fat_data, 0, 0x0FFFFFF8, 0x0FFFFFFF)
    # Cluster 2..5: Root directory cluster chain
    for c in range(2, 2 + ROOT_DIR_CLUSTERS - 1):
        struct.pack_into("<I", fat_data, c * 4, c + 1)
    struct.pack_into("<I", fat_data, (2 + ROOT_DIR_CLUSTERS - 1) * 4, 0x0FFFFFFF)

    # 3. Setup Root Directory (starts at cluster 2)
    root_dir_data = bytearray(ROOT_DIR_CLUSTERS * SECTOR_SIZE * SECTORS_PER_CLUSTER)
    root_dir_data[0:11] = b"WOOS FAT32 "
    root_dir_data[11] = 0x08  # ATTR_VOLUME_ID
    root_entry_count = 1

    current_cluster = 2 + ROOT_DIR_CLUSTERS  # Cluster 6+

    def write_cluster_data(cluster, data):
        data_sector = DATA_START + (cluster - 2) * SECTORS_PER_CLUSTER
        data_offset = data_sector * SECTOR_SIZE
        img[data_offset : data_offset + len(data)] = data

    def write_file(content):
        nonlocal current_cluster
        start_cluster = current_cluster
        cluster_bytes = SECTOR_SIZE * SECTORS_PER_CLUSTER
        num_clusters = (len(content) + cluster_bytes - 1) // cluster_bytes
        if num_clusters == 0:
            num_clusters = 1

        for i in range(num_clusters):
            clust = start_cluster + i
            slice_start = i * cluster_bytes
            slice_end = min((i + 1) * cluster_bytes, len(content))
            slice_data = content[slice_start:slice_end]
            write_cluster_data(clust, slice_data)
            next_val = 0x0FFFFFFF if i == num_clusters - 1 else clust + 1
            struct.pack_into("<I", fat_data, clust * 4, next_val)

        current_cluster += num_clusters
        return start_cluster

    # Load binaries
    def read_opt(path):
        if os.path.exists(path):
            with open(path, "rb") as f:
                return f.read()
        return b""

    comp_wasm = read_opt("apps/compositor.wasm")
    calc_wasm = read_opt("apps/calc.wasm")
    term_wasm = read_opt("apps/terminal.wasm")
    notepad_wasm = read_opt("apps/notepad.wasm")
    explorer_wasm = read_opt("apps/explorer.wasm")
    wasi_test_wasm = read_opt("apps/wasi_test.wasm")

    root_files = [
        ("HELLO.TXT", b"Hello from WoOS VFS (FAT32)!\n"),
        ("README.TXT", b"WoOS high-performance read-write FAT32 filesystem image.\n")
    ]
    if comp_wasm: root_files.append(("COMPOSIT.WAS", comp_wasm))
    if calc_wasm: root_files.append(("CALC.WAS", calc_wasm))
    if term_wasm: root_files.append(("TERM.WAS", term_wasm))
    if notepad_wasm: root_files.append(("NOTEPAD.WAS", notepad_wasm))
    if explorer_wasm: root_files.append(("EXPLORER.WAS", explorer_wasm))
    if wasi_test_wasm: root_files.append(("TESTWASI.WAS", wasi_test_wasm))

    # Write root files
    for name, content in root_files:
        start_c = write_file(content)
        parts = name.split(".")
        ent = make_entry(parts[0], parts[1] if len(parts) > 1 else "", 0x20, start_c, len(content))
        off = root_entry_count * 32
        root_dir_data[off : off + 32] = ent
        root_entry_count += 1

    # --- Setup /DESKTOP directory ---
    desktop_cluster = current_cluster
    current_cluster += 1
    struct.pack_into("<I", fat_data, desktop_cluster * 4, 0x0FFFFFFF)

    # Directory entry in root
    ent_desktop = make_entry("DESKTOP", "", 0x10, desktop_cluster, 0)
    off = root_entry_count * 32
    root_dir_data[off : off + 32] = ent_desktop
    root_entry_count += 1

    # Desktop directory cluster data
    desktop_data = bytearray(SECTOR_SIZE * SECTORS_PER_CLUSTER)
    # Entry 0: .
    desktop_data[0:32] = make_entry(".", "", 0x10, desktop_cluster, 0)
    # Entry 1: ..
    desktop_data[32:64] = make_entry("..", "", 0x10, 0, 0)
    desk_count = 2

    desktop_files = [
        ("EXPLORER.WAS", explorer_wasm),
        ("NOTEPAD.WAS", notepad_wasm),
        ("TERM.WAS", term_wasm),
        ("CALC.WAS", calc_wasm),
        ("README.TXT", b"Welcome to WoOS Desktop!\nFiles placed in /DESKTOP appear directly on your desktop screen.\n"),
        ("TESTWASI.WAS", wasi_test_wasm)
    ]

    for name, content in desktop_files:
        if not content: continue
        start_c = write_file(content)
        parts = name.split(".")
        ent = make_entry(parts[0], parts[1] if len(parts) > 1 else "", 0x20, start_c, len(content))
        d_off = desk_count * 32
        desktop_data[d_off : d_off + 32] = ent
        desk_count += 1

    write_cluster_data(desktop_cluster, desktop_data)

    # --- Setup /RECYCLE directory ---
    recycle_cluster = current_cluster
    current_cluster += 1
    struct.pack_into("<I", fat_data, recycle_cluster * 4, 0x0FFFFFFF)

    # Directory entry in root
    ent_recycle = make_entry("RECYCLE", "", 0x10, recycle_cluster, 0)
    off = root_entry_count * 32
    root_dir_data[off : off + 32] = ent_recycle
    root_entry_count += 1

    # Recycle directory cluster data
    recycle_data = bytearray(SECTOR_SIZE * SECTORS_PER_CLUSTER)
    recycle_data[0:32] = make_entry(".", "", 0x10, recycle_cluster, 0)
    recycle_data[32:64] = make_entry("..", "", 0x10, 0, 0)
    write_cluster_data(recycle_cluster, recycle_data)

    # 4. Setup Sector 1 (FSInfo)
    clusters_used = current_cluster - 2
    free_clusters = TOTAL_DATA_CLUSTERS - clusters_used
    struct.pack_into("<I480sIII14sH", img, 512,
        0x41615252,          # FSI_LeadSig ("RRaA")
        b"\x00" * 480,       # Reserved
        0x61417272,          # FSI_StrucSig ("rrAa")
        free_clusters,       # FSI_Free_Count
        current_cluster,     # FSI_Nxt_Free hint
        b"\x00" * 14,        # Reserved
        0xAA55               # BS_55AA
    )

    # 5. Backup VBR & FSInfo (Sectors 6 and 7)
    img[6 * SECTOR_SIZE : 7 * SECTOR_SIZE] = img[0 : SECTOR_SIZE]
    img[7 * SECTOR_SIZE : 8 * SECTOR_SIZE] = img[SECTOR_SIZE : 2 * SECTOR_SIZE]

    # 6. Copy FATs and Root Directory into Image
    img[FAT1_START * SECTOR_SIZE : (FAT1_START + SECTORS_PER_FAT) * SECTOR_SIZE] = fat_data
    img[FAT2_START * SECTOR_SIZE : (FAT2_START + SECTORS_PER_FAT) * SECTOR_SIZE] = fat_data
    img[DATA_START * SECTOR_SIZE : (DATA_START + ROOT_DIR_CLUSTERS) * SECTOR_SIZE] = root_dir_data

    # 7. Write woosfs.bin
    with open("woosfs.bin", "wb") as f:
        f.write(img)
    print(f"woosfs.bin created successfully (FAT32, {TOTAL_SECTORS * SECTOR_SIZE // (1024*1024)}MB, {free_clusters * SECTOR_SIZE * SECTORS_PER_CLUSTER // (1024*1024)}MB free)")

if __name__ == "__main__":
    main()
