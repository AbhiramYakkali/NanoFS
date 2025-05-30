//
// Created by Abhiram on 1/17/2025.
//

#ifndef SYSTEM_STRUCTURES_H
#define SYSTEM_STRUCTURES_H

#define _FILE 0
#define _DIRECTORY 1


#define DATA_BLOCK_FREE 0
#define DATA_BLOCK_USED 1

// Keeps track of the inode representing the current working directory
int current_working_directory = 0;

// Every command produces output if this is true
// Errors are still printed even if this is false
bool verbose = false;

struct superblock {
    int total_size, block_size, block_count, inode_size, inode_count;
};

struct inode {
    int file_size; // The number of bytes the file is using on disk
    int block_pointers[12]; // 0 indicates an unused pointer
    int is_used; // 0 = not in use
};

struct dentry {
    int inode_number;
    uint8_t file_type; // Either 0 (FILE) or 1 (DIRECTORY)
    char name[248];
};

struct superblock current_disk_superblock;

#endif //SYSTEM_STRUCTURES_H
