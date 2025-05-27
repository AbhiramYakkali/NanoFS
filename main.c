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

#define INODE_TABLE_START sizeof(struct superblock) // 0x14 by default
#define FREE_BITMAP_START (INODE_TABLE_START + DEFAULT_INODE_COUNT * sizeof(struct inode)) // 0x3c14 by default
#define DATA_START (FREE_BITMAP_START + calculate_block_count(DEFAULT_SIZE, DEFAULT_BLOCK_SIZE, DEFAULT_INODE_COUNT) / 8) //0x3c92 by defualt

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
    const double data_size = (double) total_size - sizeof(struct superblock) - (double) inode_count * sizeof(struct inode);
    // Divide by block_size + 0.125 because every data block needs a corresponding bit in the bitmap
    return floor(data_size / (block_size + 0.125));
}

int write_data_to_block(const int block_number, const void *data, const size_t data_size) {
    FILE* disk = fopen(DEFAULT_DISK_NAME, "r+b");

    const int location = DATA_START + block_number * DEFAULT_BLOCK_SIZE;

    fseek(disk, location, SEEK_SET);
    fwrite(data, 1, data_size, disk);
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
    const int bit = block_number % 8;
    const uint8_t mask = 128 >> bit;

    FILE *disk = fopen(DEFAULT_DISK_NAME, "r+b");
    const int location = FREE_BITMAP_START + byte;
    uint8_t current_bitmap_byte;

    fseek(disk, location, SEEK_SET);
    fread(&current_bitmap_byte, 1, 1, disk);

    if (status == DATA_BLOCK_USED) {
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
            if ((current_bitmap_byte & mask) == DATA_BLOCK_FREE) {
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
    const int num_dentries = (int) (cwd_inode.file_size / sizeof(struct dentry));

    if (num_dentries % DENTRIES_PER_BLOCK == 0) {
        // New data block must be allocated for the new dentry to be created
        const auto new_data_block = find_next_free_data_block();
        if (new_data_block == -1) {
            return -1;
        }

        cwd_inode.block_pointers[num_dentries / DENTRIES_PER_BLOCK] = new_data_block;
        set_data_block_status(new_data_block, 1);
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
    return (int) (directory_inode.file_size / sizeof(struct dentry));
}

// Finds all the dentries in the specified directory
int get_dentries(const int directory_number, struct dentry* destination) {
    struct inode directory_inode;
    read_inode(directory_number, &directory_inode);

    const int num_dentries = (int) (directory_inode.file_size / sizeof(struct dentry));
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
bool file_exists_in_directory(const int directory_number, const char* filename, const int expected_file_type) {
    const int num_dentries = get_num_dentries(directory_number);
    struct dentry dentries[num_dentries];
    get_dentries(current_working_directory, &dentries[0]);

    for (int i = 0; i < num_dentries; i++) {
        if (strcmp(dentries[i].name, filename) == 0 && dentries[i].file_type == expected_file_type) {
            return true;
        }
    }

    return false;
}

// Returns the number corresponding to the index of the requested file in a provided list of dentries
// Returns -1 if the file doesn't exist in the given list of dentries
int get_dentry_number_of_file(const struct dentry* dentries, const int num_dentries, const char* filename, const int expected_file_type) {
    for (int i = 0; i < num_dentries; i++) {
        if (strcmp(dentries[i].name, filename) == 0 && dentries[i].file_type == expected_file_type) {
            return i;
        }
    }

    return -1;
}

// Returns the inode number of the given file within the given directory
// Returns -1 if the file does not exist
int get_inode_number_of_file(const int directory_number, const char* filename, const int expected_file_type) {
    const int num_dentries = get_num_dentries(directory_number);
    struct dentry dentries[num_dentries];
    get_dentries(current_working_directory, &dentries[0]);

    const auto dentry_number = get_dentry_number_of_file(&dentries[0], num_dentries, filename ,expected_file_type);
    if (dentry_number == -1) return -1;

    return dentries[dentry_number].inode_number;
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
            {0, _DIRECTORY, "."},
            {0, _DIRECTORY, ".."}
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
        if (file_exists_in_directory(current_working_directory, command[1], _FILE)) {
            printf("File %s already exists in the current directory\n", command[1]);
            return 1;
        }

        // Make sure the disk has a spare inode and data block
        const auto inode_number = find_next_free_inode();

        if (inode_number == -1) {
            printf("All inodes are being used, unable to create file\n");
            return -1;
        }

        const auto data_block_number = find_next_free_data_block();

        if (data_block_number == -1) {
            printf("All data blocks are being used, unable to create file\n");
            return -1;
        }

        set_data_block_status(data_block_number, DATA_BLOCK_USED);

        struct dentry dentry = {inode_number, _FILE};
        strcpy(dentry.name, command[1]);
        struct inode inode = {0};
        inode.file_size = 0;
        inode.block_pointers[0] = data_block_number;
        inode.is_used = true;

        if (create_dentry(&dentry) == -1) {
            printf("All data blocks are being used, unable to create new dentry\n");
            set_data_block_status(data_block_number, DATA_BLOCK_FREE);
            return -1;
        }
        write_inode(inode_number, &inode);

        if (verbose) printf("Created new file %s\n", command[1]);

        return 0;
    }

    // Write command, writes command[2] to file command[1]
    // Assumes that the size of the content is less than data block size
    if (strcmp(command[0], "write") == 0) {
        const auto inode_number = get_inode_number_of_file(current_working_directory, command[1], _FILE);

        if (inode_number == -1) {
            printf("File %s does not exist in the current directory\n", command[1]);
            return 1;
        }

        struct inode inode;
        read_inode(inode_number, &inode);
        const int data_size = (int) strlen(command[2]);
        inode.file_size = data_size;

        write_inode(inode_number, &inode);
        write_data_to_block(inode.block_pointers[0], &command[2], data_size);

        return 0;
    }

    // Prints the contents of command[1] to stdout
    // Assumes that no more than one data block of data is stored in the file
    if (strcmp(command[0], "read") == 0) {
        const auto inode_number = get_inode_number_of_file(current_working_directory, command[1], _FILE);

        if (inode_number == -1) {
            printf("File %s does not exist in the current directory\n", command[1]);
            return 1;
        }

        struct inode inode;
        read_inode(inode_number, &inode);
        const auto data_size = inode.file_size;

        if (data_size == 0) {
            printf("\n");
            return 0;
        }

        char data[data_size];
        read_data_from_block(inode.block_pointers[0], &data, data_size);

        printf("%s\n", data);

        return 0;
    }

    // Creates a txt file corresponding to the specified file
    // Txt file will contain the contents of the specified file
    if (strcmp(command[0], "open") == 0) {
        const auto inode_number = get_inode_number_of_file(current_working_directory, command[1], _FILE);

        if (inode_number == -1) {
            printf("File %s does not exist in the current directory\n", command[1]);
            return 1;
        }

        struct inode inode;
        read_inode(inode_number, &inode);
        const auto data_size = inode.file_size;
        int bytes_read = 0;
        char data[data_size];

        while (bytes_read < data_size) {
            const auto bytes_to_read = __min(data_size - bytes_read, DEFAULT_BLOCK_SIZE);

            read_data_from_block(inode.block_pointers[bytes_read / DEFAULT_BLOCK_SIZE], &data[bytes_read], bytes_to_read);

            bytes_read += bytes_to_read;
        }

        char* output_file_name = command[1];
        strcat(output_file_name, ".txt");

        FILE* output_file = fopen(command[1], "wb");
        fwrite(data, 1, data_size, output_file);
        fclose(output_file);

        return 0;
    }

    // Saves the opened file command[1] on real disk into file command[2] on disk
    // Can be used to save files opened with 'open' or other files
    if (strcmp(command[0], "save") == 0) {
        FILE* input_file = fopen(command[1], "rb");

        if (input_file == NULL) {
            printf("File %s does not exist in the current real directory\n", command[1]);
        }

        const auto inode_number = get_inode_number_of_file(current_working_directory, command[2], _FILE);
        if (inode_number == -1) {
            printf("File %s does not exist in the current directory\n", command[2]);
            return 1;
        }

        struct inode inode;
        read_inode(inode_number, &inode);

        char* data[DEFAULT_BLOCK_SIZE];

        int bytes_read = 0;
        int total_bytes_read = 0;

        do {
            bytes_read = (int) fread(data, 1, DEFAULT_BLOCK_SIZE, input_file);

            auto block_number = inode.block_pointers[total_bytes_read / DEFAULT_BLOCK_SIZE];
            if (block_number == 0) {
                block_number = find_next_free_data_block();

                if (block_number == -1) {
                    printf("No free data blocks in disk, couldn't save file %s. Only saved %d bytes.\n", command[2], total_bytes_read);
                    return -1;
                }

                set_data_block_status(block_number, DATA_BLOCK_USED);
                inode.block_pointers[total_bytes_read / DEFAULT_BLOCK_SIZE] = block_number;
            }
            write_data_to_block(block_number, data, bytes_read);

            total_bytes_read += bytes_read;
        } while (bytes_read == DEFAULT_BLOCK_SIZE);

        fclose(input_file);

        inode.file_size = total_bytes_read;
        write_inode(inode_number, &inode);

        return 0;
    }

    // Create a new directory in the cwd
    if (strcmp(command[0], "mkdir") == 0) {
        if (file_exists_in_directory(current_working_directory, command[1], _DIRECTORY)) {
            printf("Directory %s exists in the current directory\n", command[1]);
            return 1;
        }

        const auto inode_number = find_next_free_inode();
        if (inode_number == -1) {
            printf("No free inode exists, unable to create directory %s\n", command[1]);
            return -1;
        }

        const auto data_block_number = find_next_free_data_block();
        if (data_block_number == -1) {
            printf("No free data block exists, couldn't create directory %s\n", command[1]);
            return -1;
        }
        set_data_block_status(data_block_number, DATA_BLOCK_USED);

        struct dentry dentry = {inode_number, _DIRECTORY};
        strcpy(dentry.name, command[1]);
        if (create_dentry(&dentry) == -1) {
            printf("All data blocks are being used, unable to create new dentry\n");
            set_data_block_status(data_block_number, DATA_BLOCK_FREE);
            return -1;
        }

        // Default dentries for a directory
        const struct dentry entries[] = {
            {inode_number, _DIRECTORY, "."},
            {current_working_directory, _DIRECTORY, ".."}
        };

        struct inode inode = {0};
        inode.file_size = sizeof(entries);
        inode.block_pointers[0] = data_block_number;
        inode.is_used = 1;

        write_inode(inode_number, &inode);

        write_data_to_block(data_block_number, entries, sizeof(entries));

        return 0;
    }

    //Remove a file
    if (strcmp(command[0], "rm") == 0) {
        const auto num_dentries = get_num_dentries(current_working_directory);
        struct dentry dentries[num_dentries];
        get_dentries(current_working_directory, &dentries[0]);

        const auto file_dentry_number = get_dentry_number_of_file(dentries, num_dentries, command[1], _FILE);
        if (file_dentry_number == -1) {
            printf("File %s does not exist in the current directory\n", command[1]);
            return 1;
        }

        // Mark all data blocks used by this file as free
        const auto inode_number = dentries[file_dentry_number].inode_number;
        struct inode inode;
        read_inode(inode_number, &inode);
        for (int i = 0; i < 12; i++) {
            if (inode.block_pointers[i] == 0) break;

            set_data_block_status(inode.block_pointers[i], DATA_BLOCK_FREE);
        }
        inode.is_used = 0;
        write_inode(inode_number, &inode);


        struct inode cwd_inode;
        read_inode(current_working_directory, &cwd_inode);

        // Remove corresponding dentry from this directory
        // Do this by overwriting corresponding dentry with the last dentry in the directory
        // Only needed if the dentry to be removed is NOT the last dentry in the list
        if (file_dentry_number != num_dentries - 1) {
            const int location = DATA_START + cwd_inode.block_pointers[(file_dentry_number / DENTRIES_PER_BLOCK)] * DEFAULT_BLOCK_SIZE + (file_dentry_number % DENTRIES_PER_BLOCK) * sizeof(struct dentry);

            FILE* disk = fopen(DEFAULT_DISK_NAME, "r+b");
            fseek(disk, location, SEEK_SET);
            fwrite(&dentries[num_dentries - 1], 1, sizeof(struct dentry), disk);
            fclose(disk);
        }

        // Update size of cwd_inode
        // TODO: Also set last data block as free if the last dentry was just copied out of it
        cwd_inode.file_size -= sizeof(struct dentry);
        write_inode(current_working_directory, &cwd_inode);

        return 0;
    }

    // Change cwd to specified directory
    if (strcmp(command[0], "cd") == 0) {
        const auto inode_number = get_inode_number_of_file(current_working_directory, command[1], _DIRECTORY);
        if (inode_number == -1) {
            printf("Directory %s does not exist in the current directory\n", command[1]);
            return 1;
        }

        current_working_directory = inode_number;

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

    //constexpr char test_args[MAX_ARGS][MAX_ARG_LEN] = {"write", "hello", "TEST!"};
    //run_fs_command(3, test_args, DEFAULT_DISK_NAME);
    //return 0;

    while (true) {
        printf("nanofs/> ");
        fflush(stdout);

        // Get command from the user
        char input[MAX_ARGS * MAX_ARG_LEN];
        fgets(input, MAX_ARGS * MAX_ARG_LEN, stdin);
        // Remove newline character
        input[strlen(input) - 1] = '\0';

        // Split the input string into words (args)
        char args[MAX_ARGS][MAX_ARG_LEN + 1];
        int arg_count = 0;
        const char *token = strtok(input, " ");

        while (token != NULL) {
            if (arg_count == 5) {
                printf("Too many arguments\n");
                break;
            }

            const auto token_length = strlen(token);
            if (token_length > MAX_ARG_LEN) {
                printf("Argument too long\n");
                break;
            }

            strncpy(args[arg_count], token, token_length);
            args[arg_count][token_length] = '\0';
            arg_count++;
            token = strtok(nullptr, " ");
        }

        run_fs_command(arg_count, args, disk_name);
    }
}