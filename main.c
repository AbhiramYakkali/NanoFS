#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system_structures.h"

// DISK STRUCTURE
// Superblock -> inodes -> free space bitmap -> data blocks

#define DEFAULT_SIZE 1048576    // 1MB
#define DEFAULT_BLOCK_SIZE 1024 // 1KB
#define DEFAULT_INODE_COUNT (DEFAULT_SIZE / 4000) // Default: 1 inode / 4KB
#define DEFAULT_DISK_NAME "nanofs_disk"

#define INODE_TABLE_START sizeof(struct superblock)
#define FREE_BITMAP_START (INODE_TABLE_START + DEFAULT_INODE_COUNT * sizeof(struct inode))
#define DATA_START (FREE_BITMAP_START + DEFAULT_INODE_COUNT / 8)

int calculate_block_count(const int total_size, const int block_size, const int inode_count) {
    const int data_size = total_size - sizeof(struct superblock) - inode_count * sizeof(struct inode);
    return data_size / block_size;
}

int write_data_to_block(const int block_number, const void *data, const size_t size) {
    FILE* disk = fopen(DEFAULT_DISK_NAME, "r+b");

    const int location = DATA_START + block_number * DEFAULT_BLOCK_SIZE;

    fseek(disk, location, SEEK_SET);
    fwrite(data, 1, size, disk);
    fclose(disk);
    return 0;
}

int read_data_from_block(const int block_number, void* buffer, const size_t size) {
    FILE* disk = fopen(DEFAULT_DISK_NAME, "rb");
    const int location = DATA_START + block_number * DEFAULT_BLOCK_SIZE;

    fseek(disk, location, SEEK_SET);
    fread(buffer, 1, size, disk);
    fclose(disk);

    return 0;
}

int run_fs_command(const int argc, const char command[5][255], const char* disk_name) {
    if (strcmp(command[0], "init") == 0) {
        // Initialize a filesystem
        const auto block_count = calculate_block_count(DEFAULT_SIZE, DEFAULT_BLOCK_SIZE, DEFAULT_INODE_COUNT);

        const struct superblock sb = {DEFAULT_SIZE, DEFAULT_BLOCK_SIZE, block_count, sizeof(struct inode), DEFAULT_INODE_COUNT};

        FILE *disk = fopen(disk_name, "wb");
        if (disk == NULL) {
            printf("Failed to open disk: %s\n", disk_name);
            return 1;
        }

        // Write superblock to the beginning of the disk
        fwrite(&sb, sizeof(struct superblock), 1, disk);

        // Create root inode (always inode 0)
        struct inode root_inode;
        root_inode.file_type = _DIRECTORY;
        root_inode.file_size = sizeof(struct dentry) * 2;
        root_inode.is_used = true;

        fwrite(&root_inode, sizeof(struct inode), 1, disk);

        // Write blank inodes to the disk next
        const struct inode inode = {0};
        for (int i = 0; i < DEFAULT_INODE_COUNT - 1; i++) {
            fwrite(&inode, sizeof(struct inode), 1, disk);
        }

        // Fill the rest of the space with empty data blocks
        char empty_data_block[DEFAULT_BLOCK_SIZE];
        for (int i = 0; i < block_count; i++) {
            fwrite(empty_data_block, sizeof(empty_data_block), 1, disk);
        }

        fclose(disk);

        // Initialize root directory's data block
        const struct dentry entries[] = {
            {0, "."},
            {0, ".."}
        };
        write_data_to_block(0, entries, sizeof(entries));

        printf("Initialized NanoFS system: %s\n", disk_name);

        return 0;
    }

    if (strcmp(command[0], "exit") == 0) {
        printf("Exiting NanoFS... Changes have been saved to %s\n", disk_name);
        exit(0);
    }

    printf("Unrecognized command: %s\n", command[0]);
    return 1;
}

int main() {
    const auto disk_name = DEFAULT_DISK_NAME;

    while (true) {
        printf("nanofs/> ");

        // Get command from the user
        char input[100];
        fgets(input, 100, stdin);
        // Remove newline character
        input[strlen(input) - 1] = '\0';

        // Split the input string into words (args)
        char args[5][255];
        int arg_count = 0;
        const char *token = strtok(input, " ");

        while (token != NULL) {
            if (arg_count == 5) {
                printf("Too many arguments\n");
                break;
            }

            strncpy(args[arg_count], token, strlen(token));
            args[arg_count][strlen(token)] = '\0';
            arg_count++;
            token = strtok(nullptr, " ");
        }

        run_fs_command(arg_count, args, disk_name);
    }
}