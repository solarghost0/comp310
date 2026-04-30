#ifndef FAT_H
#define FAT_H

/*
 * fat.h — FAT12/FAT16/FAT32 Filesystem Driver
 *
 * Supports reading FAT12, FAT16, and FAT32 volumes.
 * Designed for bare-metal / kernel environments with no stdlib dependency
 * beyond basic types. Swap out the disk_read() backend for your SD/block driver.
 */

#include <stdint.h>
#include <stddef.h>

/* ─── Error codes ───────────────────────────────────────────────────────── */
#define FAT_OK              0
#define FAT_ERR_IO         -1   /* Disk read failure                        */
#define FAT_ERR_INVAL      -2   /* Bad parameter or corrupt structure        */
#define FAT_ERR_NOENT      -3   /* File not found                           */
#define FAT_ERR_ISDIR      -4   /* Path points to a directory, not a file   */
#define FAT_ERR_NOINIT     -5   /* fatInit() not called yet                 */
#define FAT_ERR_OVERFLOW   -6   /* Buffer too small / file too large        */
#define FAT_ERR_CORRUPT    -7   /* Filesystem data is inconsistent          */

/* ─── Sector / cluster sizes ────────────────────────────────────────────── */
#define FAT_SECTOR_SIZE     512
#define FAT_MAX_SECTOR_SIZE 4096

/* ─── FAT type identifiers ──────────────────────────────────────────────── */
typedef enum {
    FAT_TYPE_UNKNOWN = 0,
    FAT_TYPE_FAT12   = 12,
    FAT_TYPE_FAT16   = 16,
    FAT_TYPE_FAT32   = 32,
} fat_type_t;

/* ─── On-disk structures (packed, little-endian) ────────────────────────── */

/* BPB = BIOS Parameter Block — first 36 bytes of boot sector (common part) */
typedef struct __attribute__((packed)) {
    uint8_t  jmp_boot[3];       /* Jump instruction to boot code            */
    uint8_t  oem_name[8];       /* OEM identifier string                    */
    uint16_t bytes_per_sector;  /* Almost always 512                        */
    uint8_t  sectors_per_clus;  /* Must be power of two, 1–128              */
    uint16_t reserved_sectors;  /* Sectors before first FAT (≥1)            */
    uint8_t  num_fats;          /* Number of FAT copies (usually 2)         */
    uint16_t root_entry_count;  /* FAT12/16: max root dir entries; FAT32: 0 */
    uint16_t total_sectors_16;  /* Total sectors if ≤ 65535; else 0         */
    uint8_t  media_type;        /* 0xF8 = fixed disk, 0xF0 = removable      */
    uint16_t fat_size_16;       /* FAT12/16: sectors per FAT; FAT32: 0      */
    uint16_t sectors_per_track; /* For interrupt 0x13 geometry              */
    uint16_t num_heads;         /* For interrupt 0x13 geometry              */
    uint32_t hidden_sectors;    /* Sectors preceding this partition         */
    uint32_t total_sectors_32;  /* Total sectors if > 65535                 */
} bpb_common_t;

/* Extended BPB for FAT12 and FAT16 */
typedef struct __attribute__((packed)) {
    bpb_common_t common;
    uint8_t  drive_num;
    uint8_t  reserved1;
    uint8_t  boot_sig;          /* 0x29 = extended boot record present      */
    uint32_t vol_id;
    uint8_t  vol_label[11];
    uint8_t  fs_type[8];        /* "FAT12   ", "FAT16   ", etc. (informational only) */
} bpb_fat16_t;

/* Extended BPB for FAT32 */
typedef struct __attribute__((packed)) {
    bpb_common_t common;
    uint32_t fat_size_32;       /* Sectors per FAT (FAT32)                  */
    uint16_t ext_flags;
    uint16_t fs_version;        /* Must be 0x0000                           */
    uint32_t root_cluster;      /* First cluster of root directory          */
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_num;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t vol_id;
    uint8_t  vol_label[11];
    uint8_t  fs_type[8];        /* "FAT32   "                               */
} bpb_fat32_t;

/* 32-byte directory entry */
typedef struct __attribute__((packed)) {
    uint8_t  name[8];           /* Short name, space-padded                 */
    uint8_t  ext[3];            /* Extension, space-padded                  */
    uint8_t  attr;              /* File attributes                          */
    uint8_t  nt_reserved;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;       /* High 16 bits of first cluster (FAT32)    */
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;       /* Low 16 bits of first cluster             */
    uint32_t file_size;         /* File size in bytes (0 for dirs)          */
} fat_dirent_t;

/* Directory entry attribute flags */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOL_ID     0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F   /* Long File Name entry                  */

/* ─── FAT filesystem state (one per mounted volume) ────────────────────── */
typedef struct {
    fat_type_t  type;               /* FAT12 / FAT16 / FAT32                */

    /* Geometry derived from BPB */
    uint32_t    bytes_per_sector;
    uint32_t    sectors_per_clus;
    uint32_t    bytes_per_clus;
    uint32_t    reserved_sectors;
    uint32_t    num_fats;
    uint32_t    fat_size;           /* Sectors per FAT                      */
    uint32_t    root_entry_count;   /* FAT12/16 only                        */
    uint32_t    total_sectors;

    /* Derived offsets (in sectors) */
    uint32_t    fat_start_sector;   /* LBA of first FAT                     */
    uint32_t    root_dir_sector;    /* FAT12/16: LBA of root dir            */
    uint32_t    root_dir_sectors;   /* FAT12/16: sectors occupied by root   */
    uint32_t    data_start_sector;  /* LBA of cluster 2                     */
    uint32_t    root_cluster;       /* FAT32: first cluster of root dir     */
    uint32_t    total_clusters;     /* Total data clusters                  */

    /* FAT table cached in memory */
    uint8_t    *fat_buf;            /* Heap/BSS buffer for the entire FAT   */
    uint32_t    fat_buf_sectors;    /* How many sectors are in fat_buf      */

    int         initialized;        /* Non-zero after fatInit() succeeds    */
} fat_fs_t;

/* ─── File handle ───────────────────────────────────────────────────────── */
typedef struct {
    fat_fs_t   *fs;
    uint32_t    first_cluster;      /* Starting cluster of the file         */
    uint32_t    file_size;          /* Size in bytes                        */
    uint32_t    current_cluster;    /* Cluster being read                   */
    uint32_t    current_cluster_idx;/* Index (0-based) of current_cluster   */
    uint32_t    pos;                /* Current byte offset within file      */
} fat_file_t;

/* ─── Public API ────────────────────────────────────────────────────────── */

/**
 * fatInit — Read the boot sector and FAT into memory.
 *
 * Reads the BPB from sector 0 of the device, validates it, detects FAT type
 * (FAT12/16/32), computes all derived offsets, and loads the entire first FAT
 * copy into fs->fat_buf.
 *
 * @param fs        Pointer to caller-allocated fat_fs_t to fill in.
 * @param fat_buf   Buffer to hold the FAT table (must be ≥ fat_size sectors).
 * @param buf_size  Size of fat_buf in bytes.
 * @return FAT_OK on success, negative FAT_ERR_* on failure.
 */
int fatInit(fat_fs_t *fs, uint8_t *fat_buf, size_t buf_size);

/**
 * fatOpen — Locate a file by 8.3 path and fill in a fat_file_t handle.
 *
 * Accepts a slash-separated 8.3 path, e.g. "/HELLO.TXT" or "DIR/FILE.TXT".
 * Names are matched case-insensitively against the on-disk short names.
 *
 * @param fs    Initialised fat_fs_t.
 * @param path  Null-terminated 8.3 path string.
 * @param file  Output file handle to fill in.
 * @return FAT_OK on success, negative FAT_ERR_* on failure.
 */
int fatOpen(fat_fs_t *fs, const char *path, fat_file_t *file);

/**
 * fatRead — Read up to count bytes from the file into buf.
 *
 * Starts reading at file->pos and advances it by the number of bytes read.
 * Follows the FAT chain cluster-by-cluster, reading whole sectors at a time
 * using the internal disk_read() backend.
 *
 * @param file   Open fat_file_t.
 * @param buf    Destination buffer.
 * @param count  Maximum bytes to read.
 * @return Number of bytes actually read (≥ 0), or negative FAT_ERR_*.
 *         Returns 0 at end-of-file.
 */
int fatRead(fat_file_t *file, void *buf, size_t count);

/* ─── Low-level disk back-end (you implement this for your hardware) ─────── */

/**
 * disk_read — Read sectors from the block device.
 *
 * You must provide this function (or replace the calls inside fat.c with
 * whatever your SD/MMC driver exposes).
 *
 * @param lba    Logical block address (sector number, 0-based).
 * @param buf    Buffer to receive sector data.
 * @param count  Number of sectors to read.
 * @return 0 on success, non-zero on error.
 */
int disk_read(uint32_t lba, void *buf, uint32_t count);

#endif /* FAT_H */