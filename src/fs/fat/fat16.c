#include "fat16.h"
#include "string/string.h"
#include "status.h"
#include "disk/disk.h"
#include "disk/streamer.h"
#include <stdint.h>
#include "kernel.h"
#include "memory/memory.h"
#include "memory/heap/kheap.h"
#include <string.h> 

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
    // Only read mode is supported
    if (mode != FILE_MODE_READ)
    {
        return ERROR(-ERDONLY);
    }

    struct fat_file_descriptor* descriptor = 0;
    descriptor = kzalloc(sizeof(struct fat_file_descriptor));

    if (!descriptor)
    {
        return ERROR(-ENOMEM);
    }

    descriptor->item = fat16_get_directory_entry(disk, path);
    if (!descriptor->item)
    {
        return ERROR(-EIO);
    }

    descriptor->pos = 0;
    return descriptor;
}


void fat16_to_proper_string(char** out, const char* in)
{
    while (*in != 0x00 && *in != 0x20)
    {
        **out = *in;
        *out += 1;
        in += 1;
    }

    if (*in == 0x20)
    {
        **out = 0x00;
    }
}

void fat16_get_full_relative_filename(struct fat_directory_item* item, char* out, int max_len)
{
    memset(out, 0x00, max_len);
    char *out_tmp = out;
    fat16_to_proper_string(&out_tmp, (const char*) item->filename);
    if (item->ext[0] != 0x00 && item->ext[0] != 0x20)
    {
        *out_tmp++ = '.';
        fat16_to_proper_string(&out_tmp, (const char*) item->ext);
    }
}
struct fat_directory_item* fat16_clone_directory_item(struct fat_directory_item* item, int size)
{
    struct fat_directory_item* item_copy = 0;

    if (size < sizeof(struct fat_directory_item))
    {
        return 0;
    }

    item_copy = kzalloc(size);
    if (!item_copy)
    {
        return 0;
    }

    memcpy(item_copy, item, size);
    return item_copy;
}
static uint32_t fat16_get_first_cluster(struct fat_directory_item* item)
{
    return (item->high_16_bits_first_cluster) | item->low_16_bits_first_cluster;
}

static uint32_t fat16_get_first_fat_sector(struct fat_private* private)
{
    return private->header.primary_header.reserved_sectors;
}

static int fat16_get_fat_entry(struct disk* disk, int cluster)
{
    int res = -1;
    struct fat_private* private = disk->fs_private;
    struct disk_stream* stream = private->fat_read_stream;

    if (!stream)
    {
        goto out;
    }

    uint32_t fat_table_position = fat16_get_first_fat_sector(private) * disk->sector_size;
    res = diskstreamer_seek(stream, fat_table_position + (cluster * OS_FAT16_FAT_ENTRY_SIZE));
    if (res < 0)
    {
        goto out;
    }

    uint16_t result = 0;
    res = diskstreamer_read(stream, &result, sizeof(result));
    if (res < 0)
    {
        goto out;
    }

    res = result;

out:
    return res;
}

/**
 * Gets the correct cluster to use based on the starting cluster and the offset
 */
static int fat16_get_cluster_for_offset(struct disk* disk, int starting_cluster, int offset)
{
    int res = 0;
    struct fat_private* private = disk->fs_private;

    // Calculate how many bytes one cluster can hold
    int size_of_cluster_bytes = private->header.primary_header.sectors_per_cluster * disk->sector_size;
    int cluster_to_use = starting_cluster;

    // Determine how many clusters ahead we need to move
    int clusters_ahead = offset / size_of_cluster_bytes;

    for (int i = 0; i < clusters_ahead; i++)
    {
        int entry = fat16_get_fat_entry(disk, cluster_to_use);

        // End of file entries in FAT16
        if (entry == 0xFF8 || entry == 0xFFF)
        {
            res = -EIO;
            goto out;
        }

        // Bad sector
        if (entry == OS_FAT16_BAD_SECTOR)
        {
            res = -EIO;
            goto out;
        }

        // Reserved or invalid cluster entries
        if (entry == 0xFF0 || entry == 0xFF6 || entry == 0x00)
        {
            res = -EIO;
            goto out;
        }

        // Move to the next cluster in the chain
        cluster_to_use = entry;
    }

    res = cluster_to_use;

out:
    return res;
}

/**
 * Reads data from the FAT16 filesystem stream
 */
static int fat16_read_internal_from_stream(struct disk* disk, struct disk_stream* stream,
                                           int cluster, int offset, int total, void* out)
{
    int res = 0;
    struct fat_private* private = disk->fs_private;

    // Bytes per cluster
    int size_of_cluster_bytes = private->header.primary_header.sectors_per_cluster * disk->sector_size;

    // Determine which cluster to read from based on offset
    int cluster_to_use = fat16_get_cluster_for_offset(disk, cluster, offset);
    if (cluster_to_use < 0)
    {
        res = cluster_to_use;
        goto out;
    }

    // Calculate offset inside the current cluster
    int offset_from_cluster = offset % size_of_cluster_bytes;

    // Find the starting sector of this cluster
    int starting_sector = fat16_cluster_to_sector(private, cluster_to_use);

    // Compute the absolute byte position on the disk
    int starting_pos = (starting_sector * disk->sector_size) + offset_from_cluster;

    // Determine how many bytes to read this round
    int total_to_read = total > size_of_cluster_bytes ? size_of_cluster_bytes : total;

    // Move the stream to the correct position
    res = diskstreamer_seek(stream, starting_pos);
    if (res != OS_ALL_OK)
    {
        goto out;
    }

    // Perform the read
    res = diskstreamer_read(stream, out, total_to_read);
    if (res != OS_ALL_OK)
    {
        goto out;
    }

    // Update how much remains
    total -= total_to_read;

    // If there's still more to read, recurse
    if (total > 0)
    {
        res = fat16_read_internal_from_stream(disk, stream, cluster, offset + total_to_read, total, (char*)out + total_to_read);
    }

out:
    return res;
}

/**
 * Internal FAT16 read wrapper.
 * Calls the lower-level function to read data from a file's cluster stream.
 */
static int fat16_read_internal(struct disk* disk, int starting_cluster, int offset, int total, void* out)
{
    struct fat_private* fs_private = disk->fs_private;
    struct disk_stream* stream = fs_private->cluster_read_stream;

    return fat16_read_internal_from_stream(disk, stream, starting_cluster, offset, total, out);
}

/**
 * Frees a FAT16 directory and its associated memory.
 */
void fat16_free_directory(struct fat_directory* directory)
{
    if (!directory)
    {
        return;
    }

    if (directory->item)
    {
        kfree(directory->item);
    }

    kfree(directory);
}

/**
 * Frees a FAT16 item (file or directory) and its associated memory.
 */
void fat16_fat_item_free(struct fat_item* item)
{
    if (item->type == FAT_ITEM_TYPE_DIRECTORY)
    {
        fat16_free_directory(item->directory);
    }
    else if (item->type == FAT_ITEM_TYPE_FILE)
    {
        kfree(item->item);
    }

    kfree(item);
}

/**
 * Loads a FAT16 directory from disk into memory.
 */
struct fat_directory* fat16_load_fat_directory(struct disk* disk, struct fat_directory_item* item)
{
    int res = 0;
    struct fat_directory* directory = 0;
    struct fat_private* fat_private = disk->fs_private;

    // Ensure the item is actually a directory
    if (!(item->attribute & FAT_FILE_SUBDIRECTORY))
    {
        res = -EINVARG;
        goto out;
    }

    // Allocate memory for directory structure
    directory = kzalloc(sizeof(struct fat_directory));
    if (!directory)
    {
        res = -ENOMEM;
        goto out;
    }

    // Get the cluster and sector for the directory
    int cluster = fat16_get_first_cluster(item);
    int cluster_sector = fat16_cluster_to_sector(fat_private, cluster);

    // Count total items inside this directory
    int total_items = fat16_get_total_items_for_directory(disk, cluster_sector);
    directory->total = total_items;

    // Allocate memory for all directory items
    int directory_size = directory->total * sizeof(struct fat_directory_item);
    directory->item = kzalloc(directory_size);
    if (!directory->item)
    {
        res = -ENOMEM;
        goto out;
    }

    // Read the entire directory into memory
    res = fat16_read_internal(disk, cluster, 0x00, directory_size, directory->item);
    if (res != OS_ALL_OK)
    {
        goto out;
    }

out:
    // Cleanup on failure
    if (res != OS_ALL_OK)
    {
        fat16_free_directory(directory);
        directory = 0;
    }

    return directory;
}

struct fat_item* fat16_new_fat_item_for_directory_item(struct disk* disk, struct fat_directory_item* item)
{
    struct fat_item* f_item = kzalloc(sizeof(struct fat_item));
    if (!f_item)
    {
        return 0;
    }

    if (item->attribute & FAT_FILE_SUBDIRECTORY)
    {
        /* It's a directory: load it and mark type accordingly */
        f_item->directory = fat16_load_fat_directory(disk, item);
        f_item->type = FAT_ITEM_TYPE_DIRECTORY;
    }
    else
    {
        /* It's a file: clone the directory item and mark type accordingly */
        f_item->type = FAT_ITEM_TYPE_FILE;
        f_item->item = fat16_clone_directory_item(item, sizeof(struct fat_directory_item));
    }

    /* Note: if fat16_load_fat_directory failed, f_item->directory may be NULL.
       Callers should handle that or you can add error handling here to free f_item. */

    return f_item;
}

struct fat_item* fat16_find_item_in_directory(struct disk* disk, struct fat_directory* directory, const char* name)
{
    struct fat_item* f_item = 0;
    char tmp_filename[PEACHOS_MAX_PATH];

    for (int i = 0; i < directory->total; i++)
    {
        fat16_get_full_relative_filename(&directory->item[i], tmp_filename, sizeof(tmp_filename));
        if (istrncmp(tmp_filename, name, sizeof(tmp_filename)) == 0)
        {
            // Found it, let’s create a new fat_item
            f_item = fat16_new_fat_item_for_directory_item(disk, &directory->item[i]);
            break; // You can add this to stop searching once found
        }
    }

    return f_item;
}


struct fat_item* fat16_get_directory_entry(struct disk* disk, struct path_part* path)
{
    struct fat_private* fat_private = disk->fs_private;
    struct fat_item* current_item = 0;

    // Start searching from the root directory
    struct fat_item* root_item = fat16_find_item_in_directory(
        disk,
        &fat_private->root_directory,
        path->part
    );

    if (!root_item)
    {
        goto out;
    }

    struct path_part* next_part = path->next;
    current_item = root_item;

    while (next_part != 0)
    {
        if (current_item->type != FAT_ITEM_TYPE_DIRECTORY)
        {
            current_item = 0;
            break;
        }

        struct fat_item* tmp_item = fat16_find_item_in_directory(
            disk,
            current_item->directory,
            next_part->part
        );

        fat16_fat_item_free(current_item);
        current_item = tmp_item;
        next_part = next_part->next;
    }

out:
    return current_item;
}


static int fat16_cluster_to_sector(struct fat_private* private, int cluster)
{
    return private->root_directory.ending_sector_pos + 
           ((cluster - 2) * private->header.primary_header.sectors_per_cluster);
}


