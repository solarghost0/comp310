/*
 * fat.c — FAT12/FAT16/FAT32 Filesystem Driver Implementation
 *
 * Design goals:
 *  - No dynamic allocation after fatInit() — the FAT table is stored in a
 *    caller-supplied buffer; everything else lives on the stack.
 *  - One sector-sized scratch buffer per operation (stack-allocated).
 *  - Works with any disk_read() backend: SD card, MMC, RAM disk, loopback.
 *  - Handles FAT12, FAT16, FAT32 transparently.
 *
 * Limitations (intentional for a kernel lab driver):
 *  - Read-only (no fatWrite / fatCreate).
 *  - 8.3 short filenames only (LFN entries are skipped).
 *  - Single open file at a time per fat_file_t handle.
 *  - No seek (reads are sequential from pos=0; extend if needed).
 */

#include "fat.h"
#include <string.h>   /* memcpy, memset, memcmp */

/* ─── Internal helpers ──────────────────────────────────────────────────── */

/* Safely read a little-endian 16-bit value from a byte pointer. */
static inline uint16_t read_le16(const uint8_t *p) {
    return (uint16_t)(p[0]) | ((uint16_t)(p[1]) << 8);
}

/* Safely read a little-endian 32-bit value from a byte pointer. */
static inline uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)(p[0])        |
           ((uint32_t)(p[1]) << 8) |
           ((uint32_t)(p[2]) << 16)|
           ((uint32_t)(p[3]) << 24);
}

/*
 * clus_to_lba — Convert a cluster number to its first LBA sector.
 * Cluster numbering starts at 2; clusters 0 and 1 are reserved.
 */
static uint32_t clus_to_lba(const fat_fs_t *fs, uint32_t cluster) {
    return fs->data_start_sector +
           (cluster - 2) * fs->sectors_per_clus;
}

/*
 * fat_next_cluster — Follow the FAT chain to the next cluster.
 *
 * Reads the appropriate entry from the in-memory FAT buffer.
 * Returns the next cluster number, or:
 *   0x0FFFFFF8 (FAT32) / 0xFFF8 (FAT16) / 0xFF8 (FAT12) — end of chain
 *   0          — error / free cluster (should not appear in a chain)
 */
static uint32_t fat_next_cluster(const fat_fs_t *fs, uint32_t cluster) {
    uint32_t offset;
    uint32_t val;

    if (fs->type == FAT_TYPE_FAT12) {
        /*
         * FAT12 packs 12-bit entries: every two entries share 3 bytes.
         * Entry N starts at byte offset (N * 3 / 2).
         * If N is even:  bits [11:0]  of the 16-bit value at offset.
         * If N is odd:   bits [15:4]  of the 16-bit value at offset.
         */
        offset = cluster + (cluster / 2);   /* byte index into FAT */
        if (offset + 1 >= fs->fat_buf_sectors * fs->bytes_per_sector)
            return 0; /* out of range */
        val = (uint32_t)read_le16(fs->fat_buf + offset);
        if (cluster & 1)
            val >>= 4;           /* odd cluster: upper 12 bits */
        else
            val &= 0x0FFF;       /* even cluster: lower 12 bits */
        /* FAT12 end-of-chain: 0xFF8–0xFFF */
        if (val >= 0x0FF8) val = 0x0FFFFFF8;
        return val;

    } else if (fs->type == FAT_TYPE_FAT16) {
        offset = cluster * 2;
        if (offset + 1 >= fs->fat_buf_sectors * fs->bytes_per_sector)
            return 0;
        val = (uint32_t)read_le16(fs->fat_buf + offset);
        /* FAT16 end-of-chain: 0xFFF8–0xFFFF */
        if (val >= 0xFFF8) val = 0x0FFFFFF8;
        return val;

    } else { /* FAT32 */
        offset = cluster * 4;
        if (offset + 3 >= fs->fat_buf_sectors * fs->bytes_per_sector)
            return 0;
        val = read_le32(fs->fat_buf + offset) & 0x0FFFFFFF;
        return val;
    }
}

/*
 * is_end_of_chain — Return non-zero if the cluster value marks end-of-chain.
 */
static int is_end_of_chain(const fat_fs_t *fs, uint32_t cluster) {
    switch (fs->type) {
        case FAT_TYPE_FAT12: return (cluster & 0x0FF8) == 0x0FF8;
        case FAT_TYPE_FAT16: return (cluster & 0xFFF8) == 0xFFF8;
        case FAT_TYPE_FAT32: return (cluster & 0x0FFFFFF8) == 0x0FFFFFF8;
        default:             return 1;
    }
}

/*
 * upcase — In-place convert ASCII lowercase to uppercase.
 * Used to normalise path components before comparing with 8.3 names.
 */
static char upcase(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/*
 * parse_83 — Parse a path component ("NAME.EXT") into an 8.3 name array
 * formatted exactly as stored in a FAT directory entry: 8 bytes name,
 * space-padded, 3 bytes extension, space-padded, no dot, all uppercase.
 *
 * Returns FAT_ERR_INVAL if the component exceeds 8.3 limits.
 */
static int parse_83(const char *component, uint8_t out[11]) {
    int i;
    const char *dot;

    memset(out, ' ', 11);

    dot = NULL;
    for (const char *p = component; *p; p++) {
        if (*p == '.') { dot = p; break; }
    }

    /* Copy name part (up to 8 chars) */
    i = 0;
    for (const char *p = component; *p && *p != '.'; p++) {
        if (i >= 8) return FAT_ERR_INVAL;
        out[i++] = (uint8_t)upcase(*p);
    }

    /* Copy extension part (up to 3 chars after the dot) */
    if (dot) {
        i = 0;
        for (const char *p = dot + 1; *p; p++) {
            if (i >= 3) return FAT_ERR_INVAL;
            out[8 + i++] = (uint8_t)upcase(*p);
        }
    }
    return FAT_OK;
}

/* ─── fatInit ────────────────────────────────────────────────────────────── */

/**
 * fatInit — Read the boot sector and FAT table into memory.
 *
 * Step-by-step:
 *  1. Read sector 0 (the Volume Boot Record / BPB).
 *  2. Validate the BPB signature (bytes 510–511 == 0x55AA).
 *  3. Extract geometry fields and derive all LBA offsets.
 *  4. Determine FAT type by counting data clusters (the canonical method
 *     per the Microsoft FAT specification).
 *  5. Copy the first FAT copy into fat_buf.
 */
int fatInit(fat_fs_t *fs, uint8_t *fat_buf, size_t buf_size) {
    uint8_t sector[FAT_SECTOR_SIZE];
    uint32_t fat_size, total_sectors, root_dir_sectors, data_sectors;

    if (!fs || !fat_buf) return FAT_ERR_INVAL;

    memset(fs, 0, sizeof(*fs));
    fs->fat_buf = fat_buf;

    /* ── 1. Read sector 0 ─────────────────────────────────────────────── */
    if (disk_read(0, sector, 1) != 0)
        return FAT_ERR_IO;

    /* ── 2. Validate boot sector signature ────────────────────────────── */
    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return FAT_ERR_INVAL;

    /* ── 3. Extract BPB fields ────────────────────────────────────────── */
    fs->bytes_per_sector  = read_le16(sector + 11);
    fs->sectors_per_clus  = sector[13];
    fs->reserved_sectors  = read_le16(sector + 14);
    fs->num_fats          = sector[16];
    fs->root_entry_count  = read_le16(sector + 17);

    /* total_sectors: use the 32-bit field if the 16-bit one is zero */
    {
        uint16_t ts16 = read_le16(sector + 19);
        uint32_t ts32 = read_le32(sector + 32);
        total_sectors  = ts16 ? ts16 : ts32;
    }

    /* fat_size: FAT32 stores it in the extended BPB at offset 36 */
    {
        uint16_t fs16 = read_le16(sector + 22);
        uint32_t fs32 = read_le32(sector + 36);
        fat_size = fs16 ? fs16 : fs32;
    }

    /* Sanity checks */
    if (fs->bytes_per_sector == 0 || fs->sectors_per_clus == 0 ||
        fs->num_fats == 0 || fat_size == 0 || total_sectors == 0)
        return FAT_ERR_INVAL;

    fs->fat_size     = fat_size;
    fs->total_sectors = total_sectors;

    /* ── 3b. Derive sector offsets ──────────────────────────────────────
     *
     *  Layout of a FAT volume:
     *  [ Reserved sectors | FAT#1 | FAT#2 | Root dir (FAT12/16) | Data ]
     *
     *  fat_start_sector = reserved_sectors
     *  root_dir_sector  = fat_start_sector + (num_fats * fat_size)
     *  root_dir_sectors = ceil(root_entry_count * 32 / bytes_per_sector)
     *  data_start_sector= root_dir_sector + root_dir_sectors
     */
    fs->fat_start_sector = fs->reserved_sectors;

    root_dir_sectors = ((uint32_t)fs->root_entry_count * 32 +
                        fs->bytes_per_sector - 1) / fs->bytes_per_sector;

    fs->root_dir_sector   = fs->fat_start_sector +
                            (fs->num_fats * fat_size);
    fs->root_dir_sectors  = root_dir_sectors;
    fs->data_start_sector = fs->root_dir_sector + root_dir_sectors;

    data_sectors = total_sectors -
                   (fs->reserved_sectors +
                    fs->num_fats * fat_size +
                    root_dir_sectors);

    fs->total_clusters    = data_sectors / fs->sectors_per_clus;
    fs->bytes_per_clus    = fs->bytes_per_sector * fs->sectors_per_clus;

    /* ── 4. Detect FAT type by cluster count (Microsoft spec §3.5) ───── */
    if (fs->total_clusters < 4085)
        fs->type = FAT_TYPE_FAT12;
    else if (fs->total_clusters < 65525)
        fs->type = FAT_TYPE_FAT16;
    else
        fs->type = FAT_TYPE_FAT32;

    /* For FAT32, the root directory begins at a cluster stored in BPB */
    if (fs->type == FAT_TYPE_FAT32)
        fs->root_cluster = read_le32(sector + 44);

    /* ── 5. Load the first FAT copy into fat_buf ──────────────────────── */
    if (buf_size < (size_t)fat_size * fs->bytes_per_sector)
        return FAT_ERR_OVERFLOW;   /* caller's buffer is too small */

    fs->fat_buf_sectors = fat_size;

    /*
     * Read FAT sectors one at a time (works even if bytes_per_sector ≠ 512).
     * For simplicity we assume disk_read operates on 512-byte sectors.
     */
    for (uint32_t s = 0; s < fat_size; s++) {
        if (disk_read(fs->fat_start_sector + s,
                      fat_buf + s * fs->bytes_per_sector, 1) != 0)
            return FAT_ERR_IO;
    }

    fs->initialized = 1;
    return FAT_OK;
}

/* ─── Directory search helper ────────────────────────────────────────────── */

/*
 * search_dir — Search a directory (given by its start cluster or, for the
 * FAT12/16 root dir, cluster == 0) for an entry matching name83[11].
 *
 * On success writes the matching fat_dirent_t into *out and returns FAT_OK.
 * Distinguishes FAT_ERR_ISDIR vs FAT_ERR_NOENT on the final component.
 */
static int search_dir(fat_fs_t *fs,
                      uint32_t  dir_cluster,  /* 0 = FAT12/16 root dir */
                      const uint8_t name83[11],
                      fat_dirent_t *out)
{
    uint8_t  sector_buf[FAT_SECTOR_SIZE];
    uint32_t lba, sector_count, current_cluster;
    int      is_root_dir = (dir_cluster == 0);

    current_cluster = dir_cluster;

    /* Iterate over all sectors of the directory */
    while (1) {
        if (is_root_dir) {
            /* FAT12/16 fixed root directory */
            lba          = fs->root_dir_sector;
            sector_count = fs->root_dir_sectors;
        } else {
            if (is_end_of_chain(fs, current_cluster))
                return FAT_ERR_NOENT;
            lba          = clus_to_lba(fs, current_cluster);
            sector_count = fs->sectors_per_clus;
        }

        for (uint32_t s = 0; s < sector_count; s++) {
            if (disk_read(lba + s, sector_buf, 1) != 0)
                return FAT_ERR_IO;

            /* 16 directory entries per 512-byte sector */
            uint32_t entries = fs->bytes_per_sector / sizeof(fat_dirent_t);
            for (uint32_t e = 0; e < entries; e++) {
                fat_dirent_t *de = (fat_dirent_t *)(sector_buf +
                                   e * sizeof(fat_dirent_t));

                /* 0x00 = no more entries in this directory */
                if (de->name[0] == 0x00)
                    return FAT_ERR_NOENT;

                /* 0xE5 = deleted entry, skip */
                if (de->name[0] == 0xE5)
                    continue;

                /* LFN entry, skip (we only handle 8.3) */
                if (de->attr == FAT_ATTR_LFN)
                    continue;

                /* Volume label, skip */
                if (de->attr & FAT_ATTR_VOL_ID)
                    continue;

                if (memcmp(de->name, name83, 11) == 0) {
                    *out = *de;
                    return FAT_OK;
                }
            }
        }

        if (is_root_dir)
            return FAT_ERR_NOENT;   /* exhausted fixed root dir */

        /* Follow FAT chain to next cluster */
        current_cluster = fat_next_cluster(fs, current_cluster);
    }
}

/* ─── fatOpen ────────────────────────────────────────────────────────────── */

/**
 * fatOpen — Locate a file by 8.3 path and initialise a fat_file_t handle.
 *
 * The path is tokenised on '/' characters. Each component is looked up in
 * the current directory. Intermediate components must be directories; the
 * final component must be a regular file (not a directory).
 */
int fatOpen(fat_fs_t *fs, const char *path, fat_file_t *file) {
    char     component[13];   /* max "XXXXXXXX.XXX\0" */
    uint8_t  name83[11];
    fat_dirent_t de;
    uint32_t  dir_cluster;
    int       ret;

    if (!fs || !fs->initialized) return FAT_ERR_NOINIT;
    if (!path || !file)          return FAT_ERR_INVAL;

    memset(file, 0, sizeof(*file));
    file->fs = fs;

    /* Start in the root directory */
    dir_cluster = (fs->type == FAT_TYPE_FAT32) ? fs->root_cluster : 0;

    /* Skip leading slash */
    if (*path == '/') path++;

    /* Walk each path component */
    while (*path) {
        /* Extract next component up to '/' or '\0' */
        int ci = 0;
        while (*path && *path != '/') {
            if (ci >= 12) return FAT_ERR_INVAL;
            component[ci++] = *path++;
        }
        component[ci] = '\0';

        if (*path == '/') path++;    /* consume separator */

        /* Convert to 8.3 */
        ret = parse_83(component, name83);
        if (ret != FAT_OK) return ret;

        /* Search current directory */
        ret = search_dir(fs, dir_cluster, name83, &de);
        if (ret != FAT_OK) return ret;

        if (*path == '\0') {
            /* Final component — must be a regular file */
            if (de.attr & FAT_ATTR_DIRECTORY)
                return FAT_ERR_ISDIR;

            /* Build the file handle */
            file->first_cluster = ((uint32_t)de.fst_clus_hi << 16) |
                                    de.fst_clus_lo;
            file->file_size         = de.file_size;
            file->current_cluster   = file->first_cluster;
            file->current_cluster_idx = 0;
            file->pos               = 0;
            return FAT_OK;

        } else {
            /* Intermediate component — must be a directory */
            if (!(de.attr & FAT_ATTR_DIRECTORY))
                return FAT_ERR_NOENT;

            dir_cluster = ((uint32_t)de.fst_clus_hi << 16) | de.fst_clus_lo;

            /* FAT12/16: cluster 0 in a directory entry means root dir */
            if (dir_cluster == 0 && fs->type != FAT_TYPE_FAT32)
                dir_cluster = 0;
        }
    }

    return FAT_ERR_NOENT;   /* empty path */
}

/* ─── fatRead ────────────────────────────────────────────────────────────── */

/**
 * fatRead — Read up to count bytes from the open file into buf.
 *
 * Algorithm:
 *  For each byte to read:
 *   • Determine which cluster contains file->pos.
 *   • If we need to advance beyond the current cluster, follow the FAT chain.
 *   • Read the appropriate sector from disk.
 *   • Copy the relevant bytes into the output buffer.
 *
 * In practice we read a full sector at a time and copy from it, so the
 * number of disk_read() calls is (bytes_to_read / bytes_per_sector) + 1.
 */
int fatRead(fat_file_t *file, void *buf, size_t count) {
    uint8_t  sector_buf[FAT_SECTOR_SIZE];
    uint8_t *out = (uint8_t *)buf;
    fat_fs_t *fs;
    uint32_t  bytes_read = 0;

    if (!file || !file->fs)     return FAT_ERR_NOINIT;
    if (!buf && count > 0)      return FAT_ERR_INVAL;

    fs = file->fs;

    /* Clamp count to remaining bytes in file */
    if (file->pos >= file->file_size) return 0;   /* EOF */
    if ((uint32_t)count > file->file_size - file->pos)
        count = file->file_size - file->pos;

    while (bytes_read < (uint32_t)count) {
        uint32_t clus_offset;    /* byte offset within current cluster */
        uint32_t sec_in_clus;    /* which sector within the cluster    */
        uint32_t byte_in_sec;    /* byte offset within that sector     */
        uint32_t lba;
        uint32_t chunk;          /* how many bytes to copy this pass   */
        uint32_t bytes_left_in_sec;

        /* ── Determine position within cluster chain ──────────────────
         *
         * file->pos gives the absolute offset in the file.
         * Divide by bytes_per_clus to get the target cluster index.
         * If the current_cluster_idx is behind, follow the FAT chain.
         */
        {
            uint32_t target_clus_idx = file->pos / fs->bytes_per_clus;

            if (target_clus_idx < file->current_cluster_idx) {
                /* Caller seeked backwards — rewind to beginning */
                file->current_cluster     = file->first_cluster;
                file->current_cluster_idx = 0;
            }

            while (file->current_cluster_idx < target_clus_idx) {
                uint32_t next = fat_next_cluster(fs, file->current_cluster);
                if (is_end_of_chain(fs, next) || next < 2)
                    return (int)bytes_read;  /* unexpected end of chain */
                file->current_cluster = next;
                file->current_cluster_idx++;
            }
        }

        /* ── Compute LBA and intra-sector offset ──────────────────────*/
        clus_offset  = file->pos % fs->bytes_per_clus;
        sec_in_clus  = clus_offset / fs->bytes_per_sector;
        byte_in_sec  = clus_offset % fs->bytes_per_sector;

        lba = clus_to_lba(fs, file->current_cluster) + sec_in_clus;

        if (disk_read(lba, sector_buf, 1) != 0)
            return FAT_ERR_IO;

        /* ── Copy as many bytes as remain in this sector ──────────────*/
        bytes_left_in_sec = fs->bytes_per_sector - byte_in_sec;
        chunk = (uint32_t)count - bytes_read;
        if (chunk > bytes_left_in_sec)
            chunk = bytes_left_in_sec;

        memcpy(out + bytes_read, sector_buf + byte_in_sec, chunk);
        bytes_read  += chunk;
        file->pos   += chunk;
    }

    return (int)bytes_read;
}