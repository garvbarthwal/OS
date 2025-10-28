#include "fat16.h"
#include "string/string.h"
#include "status.h"
#include "disk/disk.h"
#include "disk/streamer.h"
#include <stdint.h>
#include "memory/heap/kheap.h"
#include "memory/memory.h"

#define OS_FAT16_SIGNATURE      0x29
#define OS_FAT16_FAT_ENTRY_SIZE 0x02
#define OS_FAT16_BAD_SECTOR     0xFF7
#define OS_FAT16_UNUSED         0x00

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

/* Initialize per-disk private structures and streams */
static void fat16_init_private(struct disk* disk, struct fat_private* private)
{
    memset(private, 0, sizeof(struct fat_private));
    private->cluster_read_stream = diskstreamer_new(disk->id);
    private->fat_read_stream = diskstreamer_new(disk->id);
    private->directory_stream = diskstreamer_new(disk->id);
}

/* Convert sector number to absolute byte position */
int fat16_sector_to_absolute(struct disk* disk, int sector)
{
    return sector * disk->sector_size;
}

/* Count number of valid directory entries in a directory starting at sector */
int fat16_get_total_items_for_directory(struct disk* disk, uint32_t directory_start_sector)
{
    struct fat_directory_item item;
    memset(&item, 0, sizeof(item));

    struct fat_private* fat_private = disk->fs_private;
    int res = 0;
    int i = 0;
    int directory_start_pos = directory_start_sector * disk->sector_size;
    struct disk_stream* stream = fat_private->directory_stream;

    if (diskstreamer_seek(stream, directory_start_pos) != OS_ALL_OK)
    {
        res = -EIO;
        goto out;
    }

    while (1)
    {
        if (diskstreamer_read(stream, &item, sizeof(item)) != OS_ALL_OK)
        {
            res = -EIO;
            goto out;
        }

        if (item.filename[0] == 0x00)
        {
            /* End of directory */
            break;
        }

        if (item.filename[0] == 0xE5)
        {
            /* Unused entry — skip */
            continue;
        }

        i++;
    }

    res = i;

out:
    return res;
}

/* Read root directory into memory and populate directory structure */
int fat16_get_root_directory(struct disk* disk, struct fat_private* fat_private, struct fat_directory* directory)
{
    int res = 0;
    struct fat_header* primary_header = &fat_private->header.primary_header;

    int root_dir_sector_pos = (primary_header->fat_copies * primary_header->sectors_per_fat) + primary_header->reserved_sectors;
    int root_dir_entries = primary_header->root_dir_entries;
    int root_dir_size = (root_dir_entries * sizeof(struct fat_directory_item));
    int total_sectors = root_dir_size / disk->sector_size;
    if (root_dir_size % disk->sector_size)
    {
        total_sectors += 1;
    }

    int total_items = fat16_get_total_items_for_directory(disk, root_dir_sector_pos);

    struct fat_directory_item* dir = kzalloc(root_dir_size);
    if (!dir)
    {
        res = -ENOMEM;
        goto out;
    }

    struct disk_stream* stream = fat_private->directory_stream;
    if (diskstreamer_seek(stream, fat16_sector_to_absolute(disk, root_dir_sector_pos)) != OS_ALL_OK)
    {
        res = -EIO;
        goto out;
    }

    if (diskstreamer_read(stream, dir, root_dir_size) != OS_ALL_OK)
    {
        res = -EIO;
        goto out;
    }

    directory->item = dir;
    directory->total = total_items;
    directory->sector_pos = root_dir_sector_pos;
    directory->ending_sector_pos = root_dir_sector_pos + (root_dir_size / disk->sector_size);

out:
    return res;
}

/* Resolve a filesystem on the disk and initialize FAT private data */
int fat16_resolve(struct disk* disk)
{
    int res = 0;
    struct fat_private* fat_private = kzalloc(sizeof(struct fat_private));  
    fat16_init_private(disk, fat_private);  
    
    disk->fs_private = fat_private;  
    disk->filesystem = &fat16_fs;  
    
    struct disk_stream* stream = diskstreamer_new(disk->id);  
    if(!stream)  
    {  
        res = -ENOMEM;  
        goto out;  
    }  
    
    if (diskstreamer_read(stream, &fat_private->header, sizeof(  fat_private->header)) != OS_ALL_OK)  
    {  
        res = -EIO;  
        goto out;  
    }  
    
    if (fat_private->header.shared.extended_header.signature != 0x29)  
    {  
        res = -EFSNOTUS;  
        goto out;  
    }  
    
    if (fat16_get_root_directory(disk, fat_private, &fat_private->  root_directory) != OS_ALL_OK)  
    {  
        res = -EIO;  
        goto out;  
    }  
out:  
    if (stream)  
    {  
        diskstreamer_close(stream);  
    }  
    
    if (res < 0) 
    {  
        kfree(fat_private);  
        disk->fs_private = 0;  
    }  
    return res;  
} 





/* Open file in FAT16 — returns descriptor pointer or ERROR(...) */
void* fat16_open(struct disk* disk, struct path_part* path, FILE_MODE mode)
{
    if (mode != FILE_MODE_READ)
    {
        return ERROR(-ERDONLY);
    }

    struct fat_file_descriptor* descriptor = kzalloc(sizeof(struct fat_file_descriptor));
    if (!descriptor)
    {
        return ERROR(-ENOMEM);
    }

    descriptor->item = fat16_get_directory_entry(disk, path);
    if (!descriptor->item)
    {
        kfree(descriptor);
        return ERROR(-EIO);
    }

    descriptor->pos = 0;
    return descriptor;
}

static void fat16_init_private(struct disk* disk, struct fat_private*  private)  
{  
    memset(private, 0, sizeof(struct fat_private));  
    private->cluster_read_stream = diskstreamer_new(disk->id);  
    private->fat_read_stream = diskstreamer_new(disk->id);  
    private->directory_stream = diskstreamer_new(disk->id);  
} 

int fat16_sector_to_absolute(struct disk* disk, int sector)  
{  
    return sector * disk->sector_size;  
} 

int fat16_get_total_items_for_directory(struct disk* disk, uint32_t  directory_start_sector)  
{  
    struct fat_directory_item item;  
    struct fat_directory_item empty_item;  
    memset(&empty_item, 0, sizeof(empty_item));  
    
    struct fat_private* fat_private = disk->fs_private;  
    
    int res = 0;  
    int i = 0;  
    int directory_start_pos = directory_start_sector * disk->  sector_size;  
    struct disk_stream* stream = fat_private->directory_stream;  
    if(diskstreamer_seek(stream, directory_start_pos) !=  OS_ALL_OK)  
    {  
        res = -EIO;  
        goto out;  
    }

    while(1)  
    {  
        if (diskstreamer_read(stream, &item, sizeof(item)) !=  OS_ALL_OK) 
        {  
            res = -EIO;  
            goto out;  
        }  
        
        if (item.filename[0] == 0x00)  
        {  
            // We are done  
            break;  
        }  
        
        // Is the item unused  
        if (item.filename[0] == 0xE5)  
        {  
            continue;  
        }  
        
        i++;  
    }  
    
    res = i;  
    
out:  
    return res;  
} 


int fat16_get_root_directory(struct disk* disk, struct fat_private*  fat_private, struct fat_directory* directory)  
{  
    int res = 0;  
    struct fat_header* primary_header = &fat_private->header.  primary_header;  
    int root_dir_sector_pos = (primary_header->fat_copies *  primary_header->sectors_per_fat) + primary_header->  reserved_sectors;  
    int root_dir_entries = fat_private->header.primary_header.  root_dir_entries;  
    int root_dir_size = (root_dir_entries * sizeof(struct  fat_directory_item));  
    int total_sectors = root_dir_size/ disk->sector_size;  
    if (root_dir_size% disk->sector_size)  
    {  
        total_sectors += 1;  
    }
    
    int total_items = fat16_get_total_items_for_directory(disk,  root_dir_sector_pos);  
    
    struct fat_directory_item* dir = kzalloc(root_dir_size);  
    if (!dir)  
    {  
        res = -ENOMEM;  
        goto out;  
    }  
    
    struct disk_stream* stream = fat_private->directory_stream;  
    if (diskstreamer_seek(stream, fat16_sector_to_absolute(disk,  root_dir_sector_pos)) != OS_ALL_OK)  
    {  
        res = -EIO;  
        goto out;  
    }  
    
    if (diskstreamer_read(stream, dir, root_dir_size) !=  OS_ALL_OK)  
    {  
        res = -EIO;  
        goto out;  
    }  
    
    directory->item = dir;  
    directory->total = total_items;  
    directory->sector_pos = root_dir_sector_pos;  
    directory->ending_sector_pos = root_dir_sector_pos + (  root_dir_size/ disk->sector_size);  
out:  
    return res;  
} 


    






