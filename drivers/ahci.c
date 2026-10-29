#include "drivers/ahci.h"
#include "pci.h"
#include "pmm.h"
#include "serial.h"

#define ATA_CMD_READ_DMA_EXT     0x25u
#define ATA_CMD_WRITE_DMA_EXT    0x35u

#define ATA_STATUS_ERR           0x01u
#define ATA_STATUS_DRQ           0x08u
#define ATA_STATUS_BSY           0x80u

#define HBA_PORT_IPM_ACTIVE      1u
#define HBA_PORT_DET_PRESENT     3u

#define SATA_SIG_ATA             0x00000101u
#define FIS_TYPE_REG_H2D         0x27u
#define STORAGE_SECTOR_SIZE      512u

// Port Registers
typedef volatile struct hba_port {
    uint32_t clb;       // 0x00, command list base address, 1K-byte aligned
    uint32_t clbu;      // 0x04, command list base address upper 32 bits
    uint32_t fb;        // 0x08, FIS base address, 256-byte aligned
    uint32_t fbu;       // 0x0C, FIS base address upper 32 bits
    uint32_t is;        // 0x10, interrupt status
    uint32_t ie;        // 0x14, interrupt enable
    uint32_t cmd;       // 0x18, command and status
    uint32_t rsv0;      // 0x1C, Reserved
    uint32_t tfd;       // 0x20, task file data
    uint32_t sig;       // 0x24, signature
    uint32_t ssts;      // 0x28, SATA status (SCR0:SStatus)
    uint32_t sctl;      // 0x2C, SATA control (SCR2:SControl)
    uint32_t serr;      // 0x30, SATA error (SCR1:SError)
    uint32_t sact;      // 0x34, SATA active (SCR3:SActive)
    uint32_t ci;        // 0x38, command issue
    uint32_t sntf;      // 0x3C, SATA notification (SCR4:SNotification)
    uint32_t fbs;       // 0x40, FIS-based switch control
    uint32_t rsv1[11];  // 0x44 ~ 0x6F, Reserved
    uint32_t vendor[4]; // 0x70 ~ 0x7F, vendor specific
} hba_port_t;

// Generic Host Control (GHC) Registers
typedef volatile struct hba_mem {
    uint32_t cap;       // 0x00, Host capability
    uint32_t ghc;       // 0x04, Global host control
    uint32_t is;        // 0x08, Interrupt status
    uint32_t pi;        // 0x0C, Port implemented
    uint32_t vs;        // 0x10, Version
    uint32_t ccc_ctl;   // 0x14, Command completion coalescing control
    uint32_t ccc_pts;   // 0x18, Command completion coalescing ports
    uint32_t em_loc;    // 0x1C, Enclosure management location
    uint32_t em_ctl;    // 0x20, Enclosure management control
    uint32_t cap2;      // 0x24, Host capabilities extended
    uint32_t bohc;      // 0x28, BIOS/OS handoff control and status
    uint8_t  rsv[0xA0 - 0x2C]; // 0x2C - 0x9F, Reserved
    uint8_t  vendor[0x100 - 0xA0]; // 0xA0 - 0xFF, Vendor specific
    hba_port_t ports[32]; // 0x100 - 0x10FF, Port control registers
} hba_mem_t;

// Command Header
typedef struct hba_cmd_header {
    uint8_t  cfl:5;     // Command FIS length in DWORDS, 2 ~ 16
    uint8_t  a:1;       // ATAPI
    uint8_t  w:1;       // Write, 1: H2D, 0: D2H
    uint8_t  p:1;       // Prefetchable
    uint8_t  r:1;       // Reset
    uint8_t  b:1;       // BIST
    uint8_t  c:1;       // Clear busy upon R_OK
    uint8_t  rsv0:1;    // Reserved
    uint8_t  pmp:4;     // Port multiplier port
    uint16_t prdtl;     // Physical region descriptor table length in entries
    volatile uint32_t prdbc; // Physical region descriptor byte count transferred
    uint32_t ctba;      // Command table descriptor base address
    uint32_t ctbau;     // Command table descriptor base address upper 32 bits
    uint32_t rsv1[4];   // Reserved
} __attribute__((packed)) hba_cmd_header_t;

// PRDT Entry
typedef struct hba_prdt_entry {
    uint32_t dba;       // Data base address
    uint32_t dbau;      // Data base address upper 32 bits
    uint32_t rsv0;      // Reserved
    uint32_t dbc:22;    // Byte count, 4M max, 0-based
    uint32_t rsv1:9;    // Reserved
    uint32_t i:1;       // Interrupt on completion
} __attribute__((packed)) hba_prdt_entry_t;

// Command Table
typedef struct hba_cmd_tbl {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  rsv[48];
    hba_prdt_entry_t prdt_entry[1];
} __attribute__((packed)) hba_cmd_tbl_t;

// Host to Device Register FIS
typedef struct fis_reg_h2d {
    uint8_t  fis_type;  // FIS_TYPE_REG_H2D (0x27)
    uint8_t  pmport:4;  // Port multiplier
    uint8_t  rsv0:3;    // Reserved
    uint8_t  c:1;       // 1: Command, 0: Control
    uint8_t  command;   // Command register
    uint8_t  featurel;  // Feature low
    uint8_t  lba0;      // LBA low, 7:0
    uint8_t  lba1;      // LBA mid, 15:8
    uint8_t  lba2;      // LBA high, 23:16
    uint8_t  device;    // Device register (1 << 6 for LBA)
    uint8_t  lba3;      // LBA 31:24
    uint8_t  lba4;      // LBA 39:32
    uint8_t  lba5;      // LBA 47:40
    uint8_t  featureh;  // Feature high
    uint8_t  countl;    // Sector count low
    uint8_t  counth;    // Sector count high
    uint8_t  icc;       // Isochronous completion
    uint8_t  control;   // Control register
    uint8_t  rsv1[4];   // Reserved
} __attribute__((packed)) fis_reg_h2d_t;

static uint8_t g_ahci_active = 0;
static hba_mem_t* g_hba = 0;
static hba_port_t* g_active_port = 0;
static uint32_t g_active_port_idx = 0;

static void* g_ahci_page = 0;  // 4KB page: 0x000=cmd_list, 0x400=recv_fis, 0x500=cmd_tbl
static void* g_dma_buf = 0;    // 4KB bounce buffer for contiguous DMA

extern void* memset(void* s, int c, size_t n);
extern void* memcpy(void* dest, const void* src, size_t n);

static void ahci_stop_port(hba_port_t* port) {
    port->cmd &= ~(1u << 0); // Clear ST (Start)
    port->cmd &= ~(1u << 4); // Clear FRE (FIS Receive Enable)

    for (uint32_t i = 0; i < 100000u; i++) {
        if ((port->cmd & ((1u << 14) | (1u << 15))) == 0u) {
            break;
        }
    }
}

static void ahci_start_port(hba_port_t* port) {
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((port->cmd & (1u << 15)) == 0u) {
            break;
        }
    }

    port->cmd |= (1u << 4); // Set FRE
    port->cmd |= (1u << 0); // Set ST
}

static void ahci_port_rebase(hba_port_t* port) {
    ahci_stop_port(port);

    uint64_t base_phys = (uint64_t)g_ahci_page;
    port->clb = (uint32_t)base_phys;
    port->clbu = (uint32_t)(base_phys >> 32);
    memset((void*)base_phys, 0, 1024);

    uint64_t fis_phys = base_phys + 0x400;
    port->fb = (uint32_t)fis_phys;
    port->fbu = (uint32_t)(fis_phys >> 32);
    memset((void*)fis_phys, 0, 256);

    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)base_phys;
    uint64_t cmd_tbl_phys = base_phys + 0x500;
    for (int i = 0; i < 32; i++) {
        cmd_header[i].prdtl = 1;
        cmd_header[i].ctba = (uint32_t)cmd_tbl_phys;
        cmd_header[i].ctbau = (uint32_t)(cmd_tbl_phys >> 32);
    }
    memset((void*)cmd_tbl_phys, 0, 256);

    // Clear interrupt and error registers
    port->serr = port->serr;
    port->is = port->is;

    ahci_start_port(port);
}

uint8_t ahci_init(void) {
    g_ahci_active = 0;
    g_hba = 0;
    g_active_port = 0;

    pci_device_info_t ahci_pci;
    // Subclass 0x06 (SATA), Prog-IF 0x01 (AHCI 1.0)
    if (!pci_find_storage_controller(0x06, 0x01, &ahci_pci)) {
        return 0;
    }

    serial_printf("[AHCI] SATA Controller found at PCI %x:%x.%d (Vendor: 0x%x, Device: 0x%x)\n",
                  ahci_pci.bus, ahci_pci.slot, ahci_pci.func, ahci_pci.vendor_id, ahci_pci.device_id);

    // Enable Memory Space and Bus Mastering
    uint32_t cmd = pci_read_config_dword(ahci_pci.bus, ahci_pci.slot, ahci_pci.func, 0x04);
    cmd |= (1u << 1) | (1u << 2); // Memory Space + Bus Master
    pci_write_config_dword(ahci_pci.bus, ahci_pci.slot, ahci_pci.func, 0x04, cmd);

    // ABAR resides in BAR5
    uint64_t abar_phys = (uint64_t)(ahci_pci.bars[5] & ~0xFFFull);
    if (abar_phys == 0) {
        serial_printf("[AHCI] Error: BAR5 is zero!\n");
        return 0;
    }

    hba_mem_t* hba = (hba_mem_t*)abar_phys;
    g_hba = hba;

    serial_printf("[AHCI] ABAR at %p, CAP=0x%x, GHC=0x%x, PI=0x%x, VS=0x%x\n",
                  (void*)abar_phys, hba->cap, hba->ghc, hba->pi, hba->vs);

    // BIOS / OS Handoff if supported
    if (hba->cap2 & (1u << 0)) {
        hba->bohc |= (1u << 1); // Request OS Ownership
        for (uint32_t i = 0; i < 100000u; i++) {
            if ((hba->bohc & (1u << 0)) == 0u) break; // BIOS Ownership cleared
        }
    }

    // Enable AHCI Mode (GHC.AE = bit 31)
    hba->ghc |= (1u << 31);

    // Allocate physical memory pages for command structures and DMA
    if (!g_ahci_page) {
        g_ahci_page = pmm_alloc_page();
    }
    if (!g_dma_buf) {
        g_dma_buf = pmm_alloc_page();
    }

    if (!g_ahci_page || !g_dma_buf) {
        serial_printf("[AHCI] Error: Failed to allocate physical pages for AHCI!\n");
        return 0;
    }

    // Search for a connected SATA drive among implemented ports
    uint32_t pi = hba->pi;
    for (uint32_t i = 0; i < 32; i++) {
        if (pi & (1u << i)) {
            uint32_t ssts = hba->ports[i].ssts;
            uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0F);
            uint8_t det = (uint8_t)(ssts & 0x0F);

            if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
                uint32_t sig = hba->ports[i].sig;
                serial_printf("[AHCI] Port %d active, Signature: 0x%x\n", i, sig);

                if (sig == SATA_SIG_ATA) {
                    g_active_port = &hba->ports[i];
                    g_active_port_idx = i;
                    serial_printf("[AHCI] Found SATA hard drive on port %d!\n", i);
                    ahci_port_rebase(g_active_port);
                    g_ahci_active = 1;
                    return 1;
                }
            }
        }
    }

    serial_printf("[AHCI] No SATA drive attached to implemented ports.\n");
    return 0;
}

uint8_t ahci_is_active(void) {
    return g_ahci_active;
}

static uint8_t ahci_rw_sectors(uint32_t lba, uint8_t count, void* buf, uint8_t is_write) {
    if (!g_active_port || !g_ahci_page || !g_dma_buf || count == 0) {
        return 0;
    }

    hba_port_t* port = g_active_port;

    // Wait until port is ready (not busy)
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((port->tfd & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) == 0u) {
            break;
        }
    }
    if (port->tfd & (ATA_STATUS_BSY | ATA_STATUS_DRQ)) {
        serial_printf("[AHCI] Port busy before issuing command!\n");
        return 0;
    }

    port->is = port->is; // Clear interrupt status

    uint64_t base_phys = (uint64_t)g_ahci_page;
    hba_cmd_header_t* cmd_header = (hba_cmd_header_t*)base_phys;
    cmd_header[0].cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t); // 5 Dwords
    cmd_header[0].w = is_write ? 1u : 0u;
    cmd_header[0].prdtl = 1u;
    cmd_header[0].prdbc = 0u;

    hba_cmd_tbl_t* cmd_tbl = (hba_cmd_tbl_t*)(base_phys + 0x500);
    memset(cmd_tbl, 0, sizeof(hba_cmd_tbl_t));

    uint64_t dma_phys = (uint64_t)g_dma_buf;
    cmd_tbl->prdt_entry[0].dba = (uint32_t)dma_phys;
    cmd_tbl->prdt_entry[0].dbau = (uint32_t)(dma_phys >> 32);
    cmd_tbl->prdt_entry[0].dbc = (uint32_t)((count * STORAGE_SECTOR_SIZE) - 1u);
    cmd_tbl->prdt_entry[0].i = 1u;

    fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)(&cmd_tbl->cfis[0]);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1u;
    cmdfis->command = is_write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;

    cmdfis->lba0 = (uint8_t)(lba & 0xFFu);
    cmdfis->lba1 = (uint8_t)((lba >> 8) & 0xFFu);
    cmdfis->lba2 = (uint8_t)((lba >> 16) & 0xFFu);
    cmdfis->device = (1u << 6); // LBA mode

    cmdfis->lba3 = (uint8_t)((lba >> 24) & 0xFFu);
    cmdfis->lba4 = 0u;
    cmdfis->lba5 = 0u;

    cmdfis->countl = (uint8_t)(count & 0xFFu);
    cmdfis->counth = (uint8_t)((count >> 8) & 0xFFu);

    if (is_write) {
        memcpy(g_dma_buf, buf, count * STORAGE_SECTOR_SIZE);
    }

    // Issue command on slot 0
    port->ci = (1u << 0);

    // Spin wait for completion
    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if ((port->ci & (1u << 0)) == 0u) {
            break;
        }
        if (port->is & (1u << 30)) { // Task file error
            serial_printf("[AHCI] Disk error on LBA %u (is=0x%x, tfd=0x%x)\n", lba, port->is, port->tfd);
            return 0;
        }
    }

    if (port->ci & (1u << 0)) {
        serial_printf("[AHCI] Command timeout on LBA %u (ci=0x%x, tfd=0x%x)\n", lba, port->ci, port->tfd);
        return 0;
    }

    if (port->is & (1u << 30)) {
        serial_printf("[AHCI] Task file error bit set!\n");
        return 0;
    }

    if (!is_write) {
        memcpy(buf, g_dma_buf, count * STORAGE_SECTOR_SIZE);
    }

    return 1;
}

uint8_t ahci_read_sectors(uint32_t lba, uint8_t sector_count, void* buffer) {
    uint8_t* dst = (uint8_t*)buffer;
    uint32_t cur_lba = lba;
    uint8_t remaining = sector_count;

    while (remaining > 0) {
        uint8_t chunk = (remaining > 8) ? 8 : remaining;
        if (!ahci_rw_sectors(cur_lba, chunk, dst, 0)) {
            return 0;
        }
        cur_lba += chunk;
        dst += chunk * STORAGE_SECTOR_SIZE;
        remaining -= chunk;
    }
    return 1;
}

uint8_t ahci_write_sectors(uint32_t lba, uint8_t sector_count, const void* buffer) {
    const uint8_t* src = (const uint8_t*)buffer;
    uint32_t cur_lba = lba;
    uint8_t remaining = sector_count;

    while (remaining > 0) {
        uint8_t chunk = (remaining > 8) ? 8 : remaining;
        if (!ahci_rw_sectors(cur_lba, chunk, (void*)src, 1)) {
            return 0;
        }
        cur_lba += chunk;
        src += chunk * STORAGE_SECTOR_SIZE;
        remaining -= chunk;
    }
    return 1;
}
