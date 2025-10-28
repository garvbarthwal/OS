#ifndef DISK_H
#define DISK_H

struct filesystem;

typedef unsigned int OS_DISK_TYPE;

#define OS_DISK_TYPE_REAL 0

struct disk {
    OS_DISK_TYPE type;
    int sector_size;

    // Added to link filesystem with the disk
    struct filesystem* filesystem;
};

void disk_search_and_init();
struct disk* disk_get(int index);
int disk_read_block(struct disk* idisk, unsigned int lba, int total, void* buf);

#endif
