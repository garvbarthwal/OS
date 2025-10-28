#include "fat16.h"
#include "string/string.h"
#include "status.h"
#include "disk/disk.h"
#include "disk/streamer.h"
#include <stdint.h>

#define OS_FAT16_SIGNATURE 0x29
#define OS_FAT16_FAT_ENTRY_SIZE 0x02
#define OS_FAT16_BAD_SECTOR 0xFF7
#define OS_FAT16_UNUSED 0x00

typedef unsigned int FAT_ITEM_TYPE;

#define FAT_ITEM_TYPE_DIRECTORY 0
#define FAT_ITEM_TYPE_FILE 1

// FAT directory entry attribute bitmask
#define FAT_FILE_READ_ONLY 0x01
#define FAT_FILE_HIDDEN 0x02
#define FAT_FILE_SYSTEM 0x04
#define FAT_FILE_VOLUME_LABEL 0x08
#define FAT_FILE_SUBDIRECTORY 0x10
#define FAT_FILE_ARCHIVED 0x20
#define FAT_FILE_DEVICE 0x40
#define FAT_FILE_RESERVED 0x80

struct fat_header_extended
{
    uint8_t drive_number;
    uint8_t win_nt_bit;
    uint8_t signature;
    uint32_t volume_id;
    uint8_t volume_id_string[11];
    uint8_t system_id_string[8];
} __attribute__((packed));


int fat16_resolve(struct disk* disk);
void* fat16_open(struct disk* disk, struct path_part* path, FILE_MODE mode);

struct filesystem fat16_fs = {
    .resolve = fat16_resolve,
    .open = fat16_open
};

struct filesystem* fat16_init() 
{
    strcpy(fat16_fs.name, "FAT16");
    return &fat16_fs;
}

// By always returning zero we say we can read any disk regardless
// of the filesystem mounted on it.
// Placeholder for now
int fat16_resolve(struct disk* disk) 
{
    return 0;
}

void* fat16_open(struct disk* disk, struct path_part* path, FILE_MODE mode) 
{
    return 0;
}
