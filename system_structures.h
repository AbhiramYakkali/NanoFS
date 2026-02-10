//
// Created by Abhiram on 1/17/2025.
//

#ifndef SYSTEM_STRUCTURES_H
#define SYSTEM_STRUCTURES_H

#define TYPE_FILE 0
#define TYPE_DIRECTORY 1

#define DATA_BLOCK_FREE 0
#define DATA_BLOCK_USED 1

struct superblock {
    uint32_t total_size;
    uint16_t block_size, block_count, inode_size, inode_count;
};

struct inode {
    uint16_t file_size; // In bytes
    uint16_t block_pointers[12]; // 0 indicates an unused pointer
    uint8_t is_used; // 0 = not in use
};

struct dentry {
    uint16_t inode_number;
    uint8_t file_type; // Either 0 (FILE) or 1 (DIRECTORY)
    char name[253];
};

uint32_t INODE_TABLE_START, FREE_BITMAP_START, DATA_START;
uint8_t DENTRIES_PER_BLOCK;

#endif //SYSTEM_STRUCTURES_H
