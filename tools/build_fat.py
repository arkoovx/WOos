#!/usr/bin/env python3
import struct
import os
import zlib
import subprocess

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
    img[3:11] = b"WOOSFS10"
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

    def create_wallpaper_bmp(width=320, height=240):
        row_bytes = (width * 3 + 3) & ~3
        image_size = row_bytes * height
        file_size = 54 + image_size
        data = bytearray(file_size)
        data[0:2] = b'BM'
        struct.pack_into('<IHHI', data, 2, file_size, 0, 0, 54)
        struct.pack_into('<IIIHHIIIIII', data, 14,
            40, width, height, 1, 24, 0, image_size, 2835, 2835, 0, 0
        )
        for y in range(height):
            row_offset = 54 + y * row_bytes
            v = y / height
            for x in range(width):
                u = x / width
                cx = u - 0.5
                cy = v - 0.5
                dist = (cx*cx + cy*cy) ** 0.5
                r = int(max(0, min(255, 12 + 35 * (1.0 - dist) + 20 * u)))
                g = int(max(0, min(255, 25 + 75 * (1.0 - dist) + 40 * (1.0 - v))))
                b = int(max(0, min(255, 55 + 130 * (1.0 - dist) + 70 * (1.0 - u))))
                if (x % 32 == 0) or (y % 32 == 0):
                    r = min(255, r + 12)
                    g = min(255, g + 16)
                    b = min(255, b + 24)
                dx = abs(x - width // 2)
                dy = abs(y - height // 2)
                if abs(dx + dy - 24) <= 1:
                    r, g, b = 0, 210, 255
                data[row_offset + x * 3 + 0] = b
                data[row_offset + x * 3 + 1] = g
                data[row_offset + x * 3 + 2] = r
        return bytes(data)

    def make_png(width, height, raw_rgb):
        def chunk(tag, data):
            crc = zlib.crc32(tag + data) & 0xffffffff
            return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', crc)
        ihdr = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
        raw_lines = bytearray()
        for y in range(height):
            raw_lines.append(0)
            raw_lines.extend(raw_rgb[y * width * 3 : (y + 1) * width * 3])
        idat = zlib.compress(bytes(raw_lines))
        return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', idat) + chunk(b'IEND', b'')

    def create_logo_png(width=128, height=128):
        rgb = bytearray(width * height * 3)
        for y in range(height):
            for x in range(width):
                cx = (x - width // 2)
                cy = (y - height // 2)
                d = (cx*cx + cy*cy) ** 0.5
                off = (y * width + x) * 3
                if d < width * 0.45:
                    light = max(0.0, min(1.0, 1.0 - (cx*0.7 + cy*0.7 + d) / (width * 0.8)))
                    r = int(20 + 100 * light)
                    g = int(120 + 130 * light)
                    b = int(200 + 55 * light)
                else:
                    r, g, b = 30, 39, 46
                rgb[off] = r
                rgb[off + 1] = g
                rgb[off + 2] = b
        return make_png(width, height, rgb)

    def create_sample_jpg():
        tmp_jpg = '/tmp/woos_photo_sample.jpg'
        try:
            subprocess.run(['magick', '-size', '160x160', 'radial-gradient:#ff7675-#6c5ce7', tmp_jpg], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            with open(tmp_jpg, 'rb') as f:
                return f.read()
        except Exception:
            return b""

    comp_wasm = read_opt("apps/compositor.wasm")
    calc_wasm = read_opt("apps/calc.wasm")
    term_wasm = read_opt("apps/terminal.wasm")
    notepad_wasm = read_opt("apps/notepad.wasm")
    explorer_wasm = read_opt("apps/explorer.wasm")
    wasi_test_wasm = read_opt("apps/wasi_test.wasm")
    fetch_wasm = read_opt("apps/fetch.wasm")
    imgview_wasm = read_opt("apps/imageview.wasm")
    wallpap_bmp = create_wallpaper_bmp()
    logo_png = create_logo_png()
    photo_jpg = create_sample_jpg()

    root_files = [
        ("HELLO.TXT", b"Hello from WoOS VFS (FAT32)!\n"),
        ("README.TXT", b"WoOS high-performance read-write FAT32 filesystem image.\n")
    ]
    if wallpap_bmp: root_files.append(("WALLPAP.BMP", wallpap_bmp))
    if logo_png: root_files.append(("LOGO.PNG", logo_png))
    if photo_jpg: root_files.append(("PHOTO.JPG", photo_jpg))
    if comp_wasm: root_files.append(("COMPOSIT.WAS", comp_wasm))
    if calc_wasm: root_files.append(("CALC.WAS", calc_wasm))
    if term_wasm: root_files.append(("TERM.WAS", term_wasm))
    if notepad_wasm: root_files.append(("NOTEPAD.WAS", notepad_wasm))
    if explorer_wasm: root_files.append(("EXPLORER.WAS", explorer_wasm))
    if wasi_test_wasm: root_files.append(("TESTWASI.WAS", wasi_test_wasm))
    if fetch_wasm: root_files.append(("FETCH.WAS", fetch_wasm))
    if imgview_wasm: root_files.append(("IMGVIEW.WAS", imgview_wasm))

    # Write root files
    for name, content in root_files:
        start_c = write_file(content)
        parts = name.split(".")
        ent = make_entry(parts[0], parts[1] if len(parts) > 1 else "", 0x20, start_c, len(content))
        off = root_entry_count * 32
        root_dir_data[off : off + 32] = ent
        root_entry_count += 1

    # --- Setup /DESKTOP directory (4 clusters = 64 directory entries) ---
    desktop_clusters = 4
    desktop_start_cluster = current_cluster
    for i in range(desktop_clusters - 1):
        struct.pack_into("<I", fat_data, (desktop_start_cluster + i) * 4, desktop_start_cluster + i + 1)
    struct.pack_into("<I", fat_data, (desktop_start_cluster + desktop_clusters - 1) * 4, 0x0FFFFFFF)
    current_cluster += desktop_clusters

    # Directory entry in root
    ent_desktop = make_entry("DESKTOP", "", 0x10, desktop_start_cluster, 0)
    off = root_entry_count * 32
    root_dir_data[off : off + 32] = ent_desktop
    root_entry_count += 1

    # Desktop directory cluster data
    desktop_data = bytearray(desktop_clusters * SECTOR_SIZE * SECTORS_PER_CLUSTER)
    desktop_data[0:32] = make_entry(".", "", 0x10, desktop_start_cluster, 0)
    desktop_data[32:64] = make_entry("..", "", 0x10, 0, 0)
    desk_count = 2

    desktop_files = [
        ("EXPLORER.WAS", explorer_wasm),
        ("IMGVIEW.WAS", imgview_wasm),
        ("NOTEPAD.WAS", notepad_wasm),
        ("TERM.WAS", term_wasm),
        ("CALC.WAS", calc_wasm),
        ("FETCH.WAS", fetch_wasm),
        ("README.TXT", b"Welcome to WoOS Desktop!\nFiles placed in /DESKTOP appear directly on your desktop screen.\n"),
        ("WALLPAP.BMP", wallpap_bmp),
        ("LOGO.PNG", logo_png),
        ("PHOTO.JPG", photo_jpg),
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

    for i in range(desktop_clusters):
        c_slice = desktop_data[i * SECTOR_SIZE * SECTORS_PER_CLUSTER : (i + 1) * SECTOR_SIZE * SECTORS_PER_CLUSTER]
        write_cluster_data(desktop_start_cluster + i, c_slice)

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
