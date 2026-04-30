#ifndef __FAT_H__
#define __FAT_H__

#include <stdint.h>

#define CLUSTER_SIZE 4096
#define SECTORS_PER_CLUSTER (CLUSTER_SIZE/512)

#define FILE_ATTRIBUTE_SUBDIRECTORY 0x10

struct boot_sector {
    char code[3];
    char oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t num_sectors_per_cluster;
    uint16_t num_reserved_sectors;
    uint8_t num_fat_tables;
    uint16_t num_root_dir_entries;
    uint16_t total_sectors;
    uint8_t media_descriptor;
    uint16_t num_sectors_per_fat;
    uint16_t num_sectors_per_track;
    uint16_t num_heads;
    uint32_t num_hidden_sectors;
    uint32_t total_sectors_in_fs;
    uint8_t logical_drive_num;
    uint8_t reserved;
    uint8_t extended_signature;
    uint32_t serial_number;
    char volume_label[11];
    char fs_type[8];
    char boot_code[448];
    uint16_t boot_signature;
}__attribute__((packed));

struct root_directory_entry {
    char file_name[8];
    char file_extension[3];
    uint8_t attribute;
    uint8_t reserved1;
    uint8_t creation_timestamp;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t reserved2;
    uint16_t modified_time;
    uint16_t modified_date;
    uint16_t cluster;
    uint32_t file_size;
};

struct file {
    struct file *next;
    struct file *prev;
    struct root_directory_entry rde;
    uint32_t start_cluster;
};

int fatInit(void);

struct root_directory_entry* fatOpen(const char *filename);

int fatRead(struct root_directory_entry *rde, char *buffer, int buffer_size);

void extract_filename(struct root_directory_entry *rde, char *fname);
void to_uppercase(char *str);

extern struct boot_sector *bs;
extern char fat_table[128*512];
extern unsigned int root_sector;
extern unsigned int first_data_sector;

#endif
