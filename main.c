#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system_structures.h"

// DISK STRUCTURE
// Superblock -> inodes -> free space bitmap -> data blocks

#define DEFAULT_SIZE 1048576    // 1MB
#define DEFAULT_BLOCK_SIZE 1024 // 1KB
#define DEFAULT_INODE_COUNT (DEFAULT_SIZE / 4096) // 1 inode / 4KB (256 inodes default)
#define DEFAULT_DISK_NAME "nanofs_disk"

#define INODE_TABLE_START sizeof(struct superblock)
#define FREE_BITMAP_START (INODE_TABLE_START + DEFAULT_INODE_COUNT * sizeof(struct inode))
#define DATA_START (FREE_BITMAP_START + DEFAULT_INODE_COUNT / 8)

#define DENTRIES_PER_BLOCK (DEFAULT_BLOCK_SIZE / sizeof(struct dentry)) // Default: 4

#define MAX_ARGS 5
#define MAX_ARG_LEN 251

// Returns -1 if the given disk does not exist
int get_superblock(const char* disk, struct superblock* destination) {
    FILE* file = fopen(disk, "r");
    if (!file) {
        return -1;
    }

    fread(destination, sizeof(struct superblock), 1, file);
    fclose(file);

    return 0;
}

int calculate_block_count(const int total_size, const int block_size, const int inode_count) {
    const double data_size = total_size - sizeof(struct superblock) - inode_count * sizeof(struct inode);
    // Divide by block_size + 0.125 because every data block needs a corresponding bit in the bitmap
    return floor(data_size / (block_size + 0.125));
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

int read_inode(const int inode_number, struct inode* destination) {
    FILE* disk = fopen(DEFAULT_DISK_NAME, "rb");
    const int location = INODE_TABLE_START + inode_number * sizeof(struct inode);

    fseek(disk, location, SEEK_SET);
    fread(destination, 1, sizeof(struct inode), disk);
    fclose(disk);

    return 0;
}

int write_inode(const int inode_number, const struct inode* inode) {
    FILE* disk = fopen(DEFAULT_DISK_NAME, "r+b");
    const int location = INODE_TABLE_START + inode_number * sizeof(struct inode);

    fseek(disk, location, SEEK_SET);
    fwrite(inode, 1, sizeof(struct inode), disk);
    fclose(disk);

    return 0;
}

// Updates the free bitmap table to indicate if a certain block is used (1) or unused (0)
int set_data_block_status(const int block_number, const int status) {
    const int byte = block_number / 8;
    constexpr uint8_t mask = 128;

    FILE *disk = fopen(DEFAULT_DISK_NAME, "r+b");
    const int location = FREE_BITMAP_START + byte;
    uint8_t current_bitmap_byte;

    fseek(disk, location, SEEK_SET);
    fread(&current_bitmap_byte, 1, 1, disk);

    if (status == 1) {
        current_bitmap_byte |= mask;
    } else {
        current_bitmap_byte &= ~mask;
    }

    fseek(disk, location, SEEK_SET);
    fwrite(&current_bitmap_byte, 1, 1, disk);
    fclose(disk);

    return 0;
}

// Finds the first data block that is unused as specified by the bitmap
// Returns -1 if no free data blocks exist
int find_next_free_data_block() {
    FILE* disk = fopen(DEFAULT_DISK_NAME, "rb");
    constexpr int location = FREE_BITMAP_START;
    const int num_bytes_to_check = current_disk_superblock.block_size / 8;
    constexpr uint8_t mask = 1 << 7;

    fseek(disk, location, SEEK_SET);
    uint8_t current_bitmap_byte;
    for (int byte = 0; byte < num_bytes_to_check; byte++) {
        fread(&current_bitmap_byte, 1, 1, disk);

        for (int bit = 0; bit < 8; bit++) {
            if (!(current_bitmap_byte & mask)) {
                fclose(disk);
                return byte * 8 + bit;
            }

            current_bitmap_byte <<= 1;
        }
    }
    fclose(disk);

    // No free data blocks exist
    return -1;
}

// Finds the first inonde that is not being used
// Returns -1 if all inodes are being used
int find_next_free_inode() {
    // Start checking at inode 1 because inode 0 is always the root node
    for (int i = 1; i < DEFAULT_INODE_COUNT; i++) {
        struct inode inode;
        read_inode(i, &inode);

        if (!inode.is_used) return i;
    }

    return -1;
}

// Adds the specified dentry to the cwd
int create_dentry(const struct dentry* dentry) {
    struct inode cwd_inode;
    read_inode(current_working_directory, &cwd_inode);

    // The number of dentries the cwd currently has determines where the next one goes
    const int num_dentries = cwd_inode.file_size / sizeof(struct dentry);

    if (num_dentries % DENTRIES_PER_BLOCK == 0) {
        // New data block must be allocated for the new dentry to be created
        const auto new_data_block = find_next_free_data_block();

        if (new_data_block == -1) {
            return -1;
        }

        cwd_inode.block_pointers[num_dentries / DENTRIES_PER_BLOCK] = new_data_block;
    }
    cwd_inode.file_size += sizeof(struct dentry);

    const int block_number = cwd_inode.block_pointers[num_dentries / DENTRIES_PER_BLOCK];
    const int location = DATA_START + block_number * DEFAULT_BLOCK_SIZE + (num_dentries % DENTRIES_PER_BLOCK) * sizeof(struct dentry);

    FILE* disk = fopen(DEFAULT_DISK_NAME, "r+b");
    fseek(disk, location, SEEK_SET);
    fwrite(dentry, 1, sizeof(struct dentry), disk);
    fclose(disk);

    write_inode(current_working_directory, &cwd_inode);

    return 0;
}

// Returns the number of dentries in the specified directory
int get_num_dentries(const int directory_number) {
    struct inode directory_inode;
    read_inode(directory_number, &directory_inode);
    return directory_inode.file_size / sizeof(struct dentry);
}

// Finds all the dentries in the specified directory
int get_dentries(const int directory_number, struct dentry* destination) {
    struct inode directory_inode;
    read_inode(directory_number, &directory_inode);

    const int num_dentries = directory_inode.file_size / sizeof(struct dentry);
    int dentries_remaining = num_dentries; // Tracks how many dentries still need to be read
    int dentries_read = 0;

    // Acquire all dentries
    while (dentries_remaining > 0) {
        // Either read 4 dentries, or read the number left if less than 4
        int dentries_to_read = DENTRIES_PER_BLOCK;
        if (dentries_remaining < dentries_to_read) dentries_to_read = dentries_remaining;

        read_data_from_block(directory_inode.block_pointers[dentries_read / DENTRIES_PER_BLOCK], &destination[dentries_read], sizeof(struct dentry) * dentries_to_read);

        dentries_read += dentries_to_read;
        dentries_remaining -= dentries_to_read;
    }

    return 0;
}

// Checks if a file with the given name exists in the given directory
bool file_exists_in_directory(const int directory_number, const char* filename) {
    const int num_dentries = get_num_dentries(directory_number);
    struct dentry dentries[num_dentries];
    get_dentries(current_working_directory, &dentries[0]);

    for (int i = 0; i < num_dentries; i++) {
        if (strcmp(dentries[i].name, filename) == 0) {
            return true;
        }
    }

    return false;
}

int run_fs_command(const int argc, const char command[MAX_ARGS][MAX_ARG_LEN], const char* disk_name) {
    // Initialize a filesystem
    if (strcmp(command[0], "init") == 0) {
        const auto block_count = calculate_block_count(DEFAULT_SIZE, DEFAULT_BLOCK_SIZE, DEFAULT_INODE_COUNT);

        const struct superblock sb = {DEFAULT_SIZE, DEFAULT_BLOCK_SIZE, block_count, sizeof(struct inode), DEFAULT_INODE_COUNT};
        current_disk_superblock = sb;

        FILE *disk = fopen(disk_name, "wb");
        if (disk == NULL) {
            printf("Failed to open disk: %s\n", disk_name);
            return 1;
        }

        // Write superblock to the beginning of the disk
        fwrite(&sb, sizeof(struct superblock), 1, disk);

        // Create root inode (always inode 0)
        struct inode root_inode = {-1};
        root_inode.file_type = _DIRECTORY;
        root_inode.file_size = sizeof(struct dentry) * 2;
        root_inode.block_pointers[0] = 0;
        root_inode.is_used = true;

        fwrite(&root_inode, sizeof(struct inode), 1, disk);

        // Write blank inodes to the disk next
        const struct inode inode = {0};
        for (int i = 0; i < DEFAULT_INODE_COUNT - 1; i++) {
            fwrite(&inode, sizeof(struct inode), 1, disk);
        }

        // Write blank bytes for free space bitmap
        char bitmap[block_count / 8];
        memset(bitmap, 0, block_count / 8);
        fwrite(&bitmap, sizeof(bitmap), 1, disk);

        // Fill the rest of the space with empty data blocks
        const uint8_t empty_data_block[DEFAULT_BLOCK_SIZE] = {0};
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
        set_data_block_status(0, 1);

        if (verbose) printf("Initialized NanoFS system: %s\n", disk_name);

        return 0;
    }

    // List all files and directories in the current working directory
    if (strcmp(command[0], "ls") == 0) {
        const int num_dentries = get_num_dentries(current_working_directory);
        struct dentry dentries[num_dentries];

        get_dentries(current_working_directory, &dentries[0]);

        for (int i = 0; i < num_dentries; i++) {
            printf("%s ", dentries[i].name);
        }
        printf("\n");
        return 0;
    }

    // Create a new file in the cwd with name command[1]
    if (strcmp(command[0], "create") == 0) {
        // Check if a file with the same name exists in the cwd
        if (file_exists_in_directory(current_working_directory, command[1])) {
            printf("File %s already exists in the current directory\n", command[1]);
            return 1;
        }

        // Make sure the disk has a spare inode and data block
        const auto inode_number = find_next_free_inode();
        fflush(stdout);

        if (inode_number == -1) {
            printf("All inodes are being used, unable to create file\n");
            return -1;
        }

        const auto data_block_number = find_next_free_data_block();

        if (data_block_number == -1) {
            printf("All data blocks are being used, unable to create file\n");
            return -1;
        }

        struct dentry dentry = {inode_number};
        strcpy(dentry.name, command[1]);
        struct inode inode = {-1};
        inode.file_type = _FILE;
        inode.file_size = 0;
        inode.block_pointers[0] = data_block_number;
        inode.is_used = true;

        if (create_dentry(&dentry) == -1) {
            printf("All data blocks are being used, unable to create new dentry\n");
            return -1;
        }
        write_inode(inode_number, &inode);

        if (verbose) printf("Created new file %s\n", command[1]);

        return 0;
    }

    if (strcmp(command[0], "exit") == 0) {
        if (verbose) printf("Exiting NanoFS...");
        exit(0);
    }

    printf("Unrecognized command: %s\n", command[0]);
    return 1;
}

int main(const int argc, char const *argv[]) {
    if (argc > 1 && strcmp(argv[1], "verbose") == 0) verbose = true;

    const auto disk_name = DEFAULT_DISK_NAME;
    if (verbose) printf("Loading superblock for disk %s...\n", disk_name);
    const auto result = get_superblock(disk_name, &current_disk_superblock);
    if (result == -1 && verbose) {
        printf("Disk %s does not currently exist, create it using 'init' first.\n", disk_name);
    }

    while (true) {
        printf("nanofs/> ");

        // Get command from the user
        char input[MAX_ARGS * MAX_ARG_LEN];
        fgets(input, MAX_ARGS * MAX_ARG_LEN, stdin);
        // Remove newline character
        input[strlen(input) - 1] = '\0';

        // Split the input string into words (args)
        char args[MAX_ARGS][MAX_ARG_LEN];
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