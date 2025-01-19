//
// Created by Abhiram on 1/17/2025.
//

#ifndef SYSTEM_STRUCTURES_H
#define SYSTEM_STRUCTURES_H

#define _FILE 0
#define _DIRECTORY 1

struct superblock {
    int total_size, block_size, block_count, inode_size, inode_count;
};

struct inode {
    int file_type; // Either 0 (FILE) or 1 (DIRECTORY)
    int file_size;
    int block_pointers[12];
    int is_used; // 0 = not in use
};

struct dentry {
    int inode_number;
    char name[255];
};

#endif //SYSTEM_STRUCTURES_H
