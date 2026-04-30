#include "fat.h"
#include "sd.h"
#include <stdint.h>
#include <string.h>

// Forward declare our own printf
int printf(const char *fmt, ...);

char bootSector[512];
char fat_table[128*512];  // 64KB - big enough for FAT16 on a 32MB disk
struct boot_sector *bs;
char root_dir_region[32*512];

unsigned int root_sector;
unsigned int first_data_sector;

int fatInit() {
    sd_readblock(2048, bootSector, 1);
    
    bs = (struct boot_sector *)bootSector;
    
    printf(" BOOT SECTOR INFO \n");
    printf("Bytes per sector: %d\n", bs->bytes_per_sector);
    printf("Sectors per cluster: %d\n", bs->num_sectors_per_cluster);
    printf("Reserved sectors: %d\n", bs->num_reserved_sectors);
    printf("Number of FAT tables: %d\n", bs->num_fat_tables);
    printf("Root directory entries: %d\n", bs->num_root_dir_entries);
    printf("Sectors per FAT: %d\n", bs->num_sectors_per_fat);
    printf("Boot signature: 0x%X\n", bs->boot_signature);
    printf("FS Type: %.8s\n\n", bs->fs_type);
    
    if (bs->boot_signature != 0xAA55) {
        printf("ERROR: Invalid boot signature! Expected 0xAA55, got 0x%X\n", bs->boot_signature);
        return -1;
    }
    printf("y Boot signature valid (0xAA55)\n");
    
    if (strncmp(bs->fs_type, "FAT12", 5) != 0 && 
        strncmp(bs->fs_type, "FAT16", 5) != 0) {
        printf("ERROR: Invalid filesystem type! Expected FAT12 or FAT16, got %.8s\n", bs->fs_type);
        return -1;
    }
    printf("y Filesystem type valid: %.8s\n", bs->fs_type);
    
    root_sector = 2048 + bs->num_reserved_sectors + 
                  (bs->num_fat_tables * bs->num_sectors_per_fat);
    printf("Root directory sector: %d\n", root_sector);
    
    unsigned int root_dir_sectors = (bs->num_root_dir_entries * 32) / bs->bytes_per_sector;
    first_data_sector = root_sector + root_dir_sectors;
    printf("First data sector: %d\n", first_data_sector);
    
    unsigned int fat_start_sector = 2048 + bs->num_reserved_sectors;
    
    // Clamp to our buffer size
    unsigned int fat_sectors_to_read = bs->num_sectors_per_fat;
    if (fat_sectors_to_read > 128) {
        printf("WARNING: FAT is %d sectors, only reading 128\n", fat_sectors_to_read);
        fat_sectors_to_read = 128;
    }
    
    printf("Reading FAT table from sector %d (%d sectors)...\n", 
           fat_start_sector, fat_sectors_to_read);
    sd_readblock(fat_start_sector, fat_table, fat_sectors_to_read);
    
    printf(" FAT Filesystem initialized successfully!\n\n");
    return 0;
}

void extract_filename(struct root_directory_entry *rde, char *fname) {
    int k = 0;
    
    while (((rde->file_name)[k] != ' ') && (k < 8)) {
        fname[k] = (rde->file_name)[k];
        k++;
    }
    fname[k] = '\0';
    
    if ((rde->file_extension)[0] == ' ') {
        return;
    }
    
    fname[k++] = '.';
    fname[k] = '\0';
    
    int n = 0;
    while (((rde->file_extension)[n] != ' ') && (n < 3)) {
        fname[k] = (rde->file_extension)[n];
        k++;
        n++;
    }
    fname[k] = '\0';
}

void to_uppercase(char *str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] >= 'a' && str[i] <= 'z')
            str[i] = str[i] - 32;
    }
}

struct root_directory_entry* fatOpen(const char *filename) {
    printf("Opening file: %s\n", filename);
    
    char search_name[13];  // 8.3 filename max = 12 chars + null
    int j = 0;
    while (filename[j] && j < 12) {
        search_name[j] = filename[j];
        j++;
    }
    search_name[j] = '\0';
    to_uppercase(search_name);
    printf("Searching for (uppercase): %s\n", search_name);
    
    unsigned int root_dir_sectors = (bs->num_root_dir_entries * 32) / bs->bytes_per_sector;
    printf("Reading %d sectors of root directory from sector %d...\n", 
           root_dir_sectors, root_sector);
    sd_readblock(root_sector, root_dir_region, root_dir_sectors);
    
    struct root_directory_entry *rde_table = 
        (struct root_directory_entry *)root_dir_region;
    
    for (int i = 0; i < bs->num_root_dir_entries; i++) {
        if (rde_table[i].file_name[0] == 0x00) {
            break;  // No more entries
        }
        if ((unsigned char)rde_table[i].file_name[0] == 0xE5) {
            continue;  // Deleted entry
        }
        
        char current_filename[13];
        extract_filename(&rde_table[i], current_filename);
        
        if (current_filename[0] != '\0') {
            printf("  [%d] Found: %s (attr=0x%X, cluster=%d, size=%d)\n", 
                   i, current_filename, rde_table[i].attribute, 
                   rde_table[i].cluster, rde_table[i].file_size);
        }
        
        if (strcmp(current_filename, search_name) == 0) {
            if (rde_table[i].attribute & FILE_ATTRIBUTE_SUBDIRECTORY) {
                printf("ERROR: %s is a directory, not a file\n", search_name);
                return (void*)0;
            }
            printf("y File found! Cluster: %d, Size: %d bytes\n\n", 
                   rde_table[i].cluster, rde_table[i].file_size);
            return &rde_table[i];
        }
    }
    
    printf("ERROR: File %s not found in root directory\n\n", search_name);
    return (void*)0;
}

int fatRead(struct root_directory_entry *rde, char *buffer, int buffer_size) {
    if (rde == (void*)0) {
        printf("ERROR: RDE is NULL\n");
        return -1;
    }
    
    printf("Reading file of size %d bytes...\n", rde->file_size);
    
    int bytes_to_read = (rde->file_size < (unsigned int)buffer_size) ? rde->file_size : buffer_size;
    int bytes_read = 0;
    
    unsigned int current_cluster = rde->cluster;
    int cluster_size = bs->num_sectors_per_cluster * bs->bytes_per_sector;
    
    while (current_cluster >= 2 && current_cluster < 0xFFF8 && bytes_read < bytes_to_read) {
        printf("Reading cluster %d...\n", current_cluster);
        
        unsigned int cluster_sector = first_data_sector + 
                                      (current_cluster - 2) * bs->num_sectors_per_cluster;
        
        sd_readblock(cluster_sector, buffer + bytes_read, bs->num_sectors_per_cluster);
        
        int bytes_in_cluster = (cluster_size < (bytes_to_read - bytes_read)) ? 
                               cluster_size : (bytes_to_read - bytes_read);
        bytes_read += bytes_in_cluster;
        
        current_cluster = ((uint16_t *)fat_table)[current_cluster];
        
        printf("  Next cluster from FAT: 0x%X\n", current_cluster);
    }
    
    printf("y Read %d bytes from file\n\n", bytes_read);
    return bytes_read;
}
