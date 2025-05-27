#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>     
#include <unistd.h>    
#include <sys/types.h> 

//File system structure
#define VSFS_MAGIC       0xD34D  // Magic bytes for VSFS
#define BLOCK_SIZE       4096    // Size of each block in bytes
#define TOTAL_BLOCKS     64      // Total number of blocks in file system
#define INODE_SIZE       256     // Size of each inode in bytes
#define INODE_COUNT      (5 * BLOCK_SIZE / INODE_SIZE)  // Number of inodes (80)

// Block numbers
#define SUPERBLOCK_BLOCK_NUM     0   // Block number for superblock
#define INODE_BITMAP_BLOCK_NUM   1   // Block number for inode bitmap
#define DATA_BITMAP_BLOCK_NUM    2   // Block number for data bitmap
#define INODE_TABLE_START_BLOCK  3   // Start block number for inode table
#define DATA_BLOCK_START         8   // Start block number for data blocks

// Superblock structure
typedef struct {
    uint16_t magic;              // Magic number (0xD34D)
    uint32_t block_size;         // Size of each block (4096)
    uint32_t total_blocks;       // Total number of blocks (64)
    uint32_t inode_bitmap_block; // Block number of inode bitmap (1)
    uint32_t data_bitmap_block;  // Block number of data bitmap (2)
    uint32_t inode_table_block;  // Starting block number of inode table (3)
    uint32_t data_block_start;   // Starting block number of data blocks (8)
    uint32_t inode_size;         // Size of each inode (256)
    uint32_t inode_count;        // Number of inodes (80)
    uint8_t reserved[4058];      // Reserved space
} superblock_t;

// Inode structure
typedef struct {
    uint32_t mode;               // File mode
    uint32_t uid;                // User ID of owner
    uint32_t gid;                // Group ID of owner
    uint32_t size;               // File size in bytes
    uint32_t atime;              // Last access time
    uint32_t ctime;              // Creation time
    uint32_t mtime;              // Last modification time
    uint32_t dtime;              // Deletion time
    uint32_t links_count;        // Number of hard links to this inode
    uint32_t blocks_count;       // Number of data blocks allocated
    uint32_t direct_block;       // Direct block pointer
    uint32_t single_indirect;    // Single indirect block pointer
    uint32_t double_indirect;    // Double indirect block pointer
    uint32_t triple_indirect;    // Triple indirect block pointer
    uint8_t reserved[156];       // Reserved space
} inode_t;

// Global variables
char *fs_image_path = "vsfs.img";   // Path to the file system image
int fs_fd = -1;                     // File descriptor for the file system image
superblock_t superblock;            // Superblock of the file system
uint8_t inode_bitmap[BLOCK_SIZE];   // Inode bitmap
uint8_t data_bitmap[BLOCK_SIZE];    // Data bitmap
bool used_blocks[TOTAL_BLOCKS];     // Tracks blocks used by inodes
bool duplicated_blocks[TOTAL_BLOCKS]; // Tracks duplicated blocks


bool open_fs_image();
void close_fs_image();
bool read_block(int block_num, void *buffer);
bool write_block(int block_num, void *buffer);
bool check_superblock();
bool check_inode_bitmap();
bool check_data_bitmap();
bool check_duplicates();
bool check_bad_blocks();
void print_fsck_results();
void fix_errors();
bool is_valid_inode(inode_t *inode);
bool is_used_bit(uint8_t *bitmap, int bit_index);
void set_bit(uint8_t *bitmap, int bit_index);
void clear_bit(uint8_t *bitmap, int bit_index);
int allocate_new_data_block();
void check_indirect_block(uint32_t block_num, int level, int inode_num);



int superblock_errors = 0;
int inode_bitmap_errors = 0;
int data_bitmap_errors = 0;
int duplicate_block_errors = 0;
int bad_block_errors = 0;


int main() {
    printf("VSFS Consistency Checker (vsfsck)\n");
    printf("----------------------------------\n");

    // Initialize used and duplicated blocks arrays
    memset(used_blocks, 0, sizeof(used_blocks));
    memset(duplicated_blocks, 0, sizeof(duplicated_blocks));

    
    if (!open_fs_image()) {
        printf("Failed to open file system image: %s\n", fs_image_path);
        return EXIT_FAILURE;
    }

    //Checking the file system
    bool superblock_ok = check_superblock();
    bool inode_bitmap_ok = check_inode_bitmap();
    bool data_bitmap_ok = check_data_bitmap();
    bool duplicates_ok = check_duplicates();
    bool bad_blocks_ok = check_bad_blocks();

    
    print_fsck_results();

    
    if (!superblock_ok || !inode_bitmap_ok || !data_bitmap_ok || !duplicates_ok || !bad_blocks_ok) {
                fix_errors();
        
        //Recheck
        superblock_errors = 0;
        inode_bitmap_errors = 0;
        data_bitmap_errors = 0;
        duplicate_block_errors = 0;
        bad_block_errors = 0;
        
        check_superblock();
        check_inode_bitmap();
        check_data_bitmap();
        check_duplicates();
        check_bad_blocks();
        
        printf("\nRechecking after fixes...\n");
        print_fsck_results();
    }

    
    close_fs_image();
    return EXIT_SUCCESS;
}

// File image read+write
bool open_fs_image() {
    fs_fd = open(fs_image_path, O_RDWR);
    return (fs_fd != -1);
}

//file image close
void close_fs_image() {
    if (fs_fd != -1) {
        close(fs_fd);
        fs_fd = -1;
    }
}


bool read_block(int block_num, void *buffer) {
    if (fs_fd == -1 || block_num < 0 || block_num >= TOTAL_BLOCKS) {
        return false;
    }

    // Seek to the block
    off_t offset = (off_t)block_num * BLOCK_SIZE;
    if (lseek(fs_fd, offset, SEEK_SET) == -1) {
        return false;
    }

    // Read the block
    ssize_t bytes_read = read(fs_fd, buffer, BLOCK_SIZE);
    return (bytes_read == BLOCK_SIZE);
}

bool write_block(int block_num, void *buffer) {
    if (fs_fd == -1 || block_num < 0 || block_num >= TOTAL_BLOCKS) {
        return false;
    }

    // Seek to the block
    off_t offset = (off_t)block_num * BLOCK_SIZE;
    if (lseek(fs_fd, offset, SEEK_SET) == -1) {
        return false;
    }

    // Write the block
    ssize_t bytes_written = write(fs_fd, buffer, BLOCK_SIZE);
    return (bytes_written == BLOCK_SIZE);
}



//Checks if a bit is set in the bitmap

bool is_used_bit(uint8_t *bitmap, int bit_index) {
    int byte_index = bit_index / 8;
    int bit_offset = bit_index % 8;
    return (bitmap[byte_index] & (1 << bit_offset)) != 0;
}


//Sets a bit in the bitmap

void set_bit(uint8_t *bitmap, int bit_index) {
    int byte_index = bit_index / 8;
    int bit_offset = bit_index % 8;
    bitmap[byte_index] |= (1 << bit_offset);
}


//Clears a bit in the bitmap

void clear_bit(uint8_t *bitmap, int bit_index) {
    int byte_index = bit_index / 8;
    int bit_offset = bit_index % 8;
    bitmap[byte_index] &= ~(1 << bit_offset);
}


bool is_valid_inode(inode_t *inode) {
    return ((*inode).links_count > 0 && (*inode).dtime == 0);
}


bool check_superblock() {
    
    if (!read_block(SUPERBLOCK_BLOCK_NUM, &superblock)) {
        printf("Error reading superblock\n");
        superblock_errors++;
        return false;
    }

    bool is_valid = true;

    
    if (superblock.magic != VSFS_MAGIC) {
        printf("Error: Invalid superblock magic number (0x%04X, expected 0x%04X)\n",
               superblock.magic, VSFS_MAGIC);
        is_valid = false;
        superblock_errors++;
    }

    // Check block size
    if (superblock.block_size != BLOCK_SIZE) {
        printf("Error: Invalid block size (%u, expected %u)\n",
               superblock.block_size, BLOCK_SIZE);
        is_valid = false;
        superblock_errors++;
    }

    // Check total blocks
    if (superblock.total_blocks != TOTAL_BLOCKS) {
        printf("Error: Invalid total blocks (%u, expected %u)\n",
               superblock.total_blocks, TOTAL_BLOCKS);
        is_valid = false;
        superblock_errors++;
    }

    // Check inode bitmap block number
    if (superblock.inode_bitmap_block != INODE_BITMAP_BLOCK_NUM) {
        printf("Error: Invalid inode bitmap block number (%u, expected %u)\n",
               superblock.inode_bitmap_block, INODE_BITMAP_BLOCK_NUM);
        is_valid = false;
        superblock_errors++;
    }

    // Check data bitmap block number
    if (superblock.data_bitmap_block != DATA_BITMAP_BLOCK_NUM) {
        printf("Error: Invalid data bitmap block number (%u, expected %u)\n",
               superblock.data_bitmap_block, DATA_BITMAP_BLOCK_NUM);
        is_valid = false;
        superblock_errors++;
    }

    // Check inode table start block number
    if (superblock.inode_table_block != INODE_TABLE_START_BLOCK) {
        printf("Error: Invalid inode table start block number (%u, expected %u)\n",
               superblock.inode_table_block, INODE_TABLE_START_BLOCK);
        is_valid = false;
        superblock_errors++;
    }

    // Check data block start number
    if (superblock.data_block_start != DATA_BLOCK_START) {
        printf("Error: Invalid data block start number (%u, expected %u)\n",
               superblock.data_block_start, DATA_BLOCK_START);
        is_valid = false;
        superblock_errors++;
    }

    // Check inode size
    if (superblock.inode_size != INODE_SIZE) {
        printf("Error: Invalid inode size (%u, expected %u)\n",
               superblock.inode_size, INODE_SIZE);
        is_valid = false;
        superblock_errors++;
    }

    // Check inode count
    if (superblock.inode_count != INODE_COUNT) {
        printf("Error: Invalid inode count (%u, expected %u)\n",
               superblock.inode_count, INODE_COUNT);
        is_valid = false;
        superblock_errors++;
    }

    if (is_valid) {
        printf("Superblock check: PASSED\n");
    } else {
        printf("Superblock check: FAILED\n");
    }

    return is_valid;
}





bool check_inode_bitmap() {
    printf("Checking inode bitmap...\n");
    
    inode_bitmap_errors = 0;
    int type1_errors = 0;  // Invalid inodes marked used
    int type2_errors = 0;  // Valid inodes not marked used
    
    // Read the inode bitmap from disk
    if (!read_block(INODE_BITMAP_BLOCK_NUM, inode_bitmap)) {
        fprintf(stderr, "Error reading inode bitmap\n");
        inode_bitmap_errors++;
        return false;
    }
    
    // Check each inode
    for (int i = 0; i < INODE_COUNT; i++) {
        inode_t inode;
        int block_num = INODE_TABLE_START_BLOCK + (i * INODE_SIZE) / BLOCK_SIZE;
        int offset = (i * INODE_SIZE) % BLOCK_SIZE;
        
        uint8_t block[BLOCK_SIZE];
        if (!read_block(block_num, block)) {
            printf("Error reading inode table block %d\n", block_num);
            inode_bitmap_errors++;
            continue;
        }
        
        memcpy(&inode, block + offset, sizeof(inode_t));
        
        // Determine if inode is valid
        bool valid = is_valid_inode(&inode);
        
        // Type 1 error: Inode is marked as used but is invalid
        if (is_used_bit(inode_bitmap, i) && !valid) {
            printf("Error: Inode %d is marked as used but is invalid\n", i);
            type1_errors++;
        } 
        // Type 2 error: Inode is valid but not marked as used
        else if (!is_used_bit(inode_bitmap, i) && valid) {
            printf("Error: Inode %d is valid but not marked as used\n", i);
            type2_errors++;
        }
    }
    
    
    inode_bitmap_errors = type1_errors + type2_errors;
    
    
    if (inode_bitmap_errors > 0) {
        printf("Inode bitmap errors summary: %d errors\n", inode_bitmap_errors);
        printf("  - Invalid inodes marked as used: %d\n", type1_errors);
        printf("  - Valid inodes not marked as used: %d\n", type2_errors);
    }
    
    return (inode_bitmap_errors == 0);
}
 
//Checks data bitmap consistency

bool check_data_bitmap() {
    
    if (!read_block(DATA_BITMAP_BLOCK_NUM, data_bitmap)) {
        fprintf(stderr, "Error reading data bitmap\n");
        data_bitmap_errors++;
        return false;
    }

    bool is_valid = true;

    // Check that every block marked as used in the bitmap is actually used
    for (uint32_t i = DATA_BLOCK_START; i < TOTAL_BLOCKS; i++) {
        if (is_used_bit(data_bitmap, i - DATA_BLOCK_START)) {
            if (!used_blocks[i]) {
                printf("Error: Block %u is marked as used in bitmap but not actually used\n", i);
                is_valid = false;
                data_bitmap_errors++;
            }
        } else {
            // Block is marked as free, but actually used
            if (used_blocks[i]) {
                printf("Error: Block %u is used but not marked in bitmap\n", i);
                is_valid = false;
                data_bitmap_errors++;
            }
        }
    }

    if (is_valid) {
        printf("Data bitmap check: PASSED\n");
    } else {
        printf("Data bitmap check: FAILED\n");
    }

    return is_valid;
}

//Checks for duplicate block references
bool check_duplicates() {
    bool is_valid = true;
    for (uint32_t i = DATA_BLOCK_START; i < TOTAL_BLOCKS; i++) {
        if (duplicated_blocks[i]) {
            printf("Error: Block %u is referenced by multiple inodes\n", i);
            is_valid = false;
        }
    }

    if (is_valid) {
        printf("Duplicate blocks check: PASSED\n");
    } else {
        printf("Duplicate blocks check: FAILED\n");
    }

    return is_valid;
}

void check_indirect_block(uint32_t block_num, int level, int inode_num) {
    if (block_num < DATA_BLOCK_START || block_num >= TOTAL_BLOCKS) {
        printf("Error: Inode %d has invalid level-%d indirect block %u\n",
               inode_num, level, block_num);
        bad_block_errors++;
        return;
    }
    uint32_t block_pointers[BLOCK_SIZE / sizeof(uint32_t)];
    if (!read_block(block_num, block_pointers)) {
        printf("Error: Could not read indirect block %u (level %d) for inode %d\n",
               block_num, level, inode_num);
        bad_block_errors++;
        return;
    }
    int num_pointers = BLOCK_SIZE / sizeof(uint32_t);
    for (int i = 0; i < num_pointers; i++) {
        if (block_pointers[i] != 0) {
            if (block_pointers[i] < DATA_BLOCK_START || block_pointers[i] >= TOTAL_BLOCKS) {
                printf("Error: Inode %d has invalid block pointer %u in level-%d indirect block %u\n",
                       inode_num, block_pointers[i], level, block_num);
                bad_block_errors++;
            } else {
                used_blocks[block_pointers[i]] = true;
                if (level > 1) {
                    check_indirect_block(block_pointers[i], level - 1, inode_num);
                }
            }
        }
    }
}
//Checks for blocks outside valid range
bool check_bad_blocks() {
    bad_block_errors = 0;
    
    //reset the used blocks
    memset(used_blocks, 0, sizeof(used_blocks));
    
    // Check all inodes for bad block references
    for (int i = 0; i < INODE_COUNT; i++) {
        inode_t inode;
        int block_num = INODE_TABLE_START_BLOCK + (i * INODE_SIZE) / BLOCK_SIZE;
        int offset = (i * INODE_SIZE) % BLOCK_SIZE;
        
        uint8_t block[BLOCK_SIZE];
        if (!read_block(block_num, block)) {
            printf("Error reading inode table block %d\n", block_num);
            continue;
        }
        
        memcpy(&inode, block + offset, sizeof(inode_t));
        
        
        if (!is_valid_inode(&inode)) {
            continue;
        }
        
        // Check direct block
        if (inode.direct_block != 0) {
            if (inode.direct_block < DATA_BLOCK_START || inode.direct_block >= TOTAL_BLOCKS) {
                printf("Error: Inode %d has invalid direct block %u (valid range: %u-%u)\n",
                       i, inode.direct_block, DATA_BLOCK_START, TOTAL_BLOCKS-1);
                bad_block_errors++;
            } else {
                used_blocks[inode.direct_block] = true;
            }
        }
        
        // Check single indirect block
        if (inode.single_indirect != 0) {
            if (inode.single_indirect < DATA_BLOCK_START || inode.single_indirect >= TOTAL_BLOCKS) {
                printf("Error: Inode %d has invalid single indirect block %u\n",
                       i, inode.single_indirect);
                bad_block_errors++;
            } else {
                used_blocks[inode.single_indirect] = true;
                check_indirect_block(inode.single_indirect, 1, i);
            }
        }
        
        // Check double indirect block
        if (inode.double_indirect != 0) {
            if (inode.double_indirect < DATA_BLOCK_START || inode.double_indirect >= TOTAL_BLOCKS) {
                printf("Error: Inode %d has invalid double indirect block %u\n",
                       i, inode.double_indirect);
                bad_block_errors++;
            } else {
                used_blocks[inode.double_indirect] = true;
                check_indirect_block(inode.double_indirect, 2, i);
            }
        }
        
        // Check triple indirect block
        if (inode.triple_indirect != 0) {
            if (inode.triple_indirect < DATA_BLOCK_START || inode.triple_indirect >= TOTAL_BLOCKS) {
                printf("Error: Inode %d has invalid triple indirect block %u\n",
                       i, inode.triple_indirect);
                bad_block_errors++;
            } else {
                used_blocks[inode.triple_indirect] = true;
                check_indirect_block(inode.triple_indirect, 3, i);
            }
        }
    }
    
    if (bad_block_errors > 0) {
        printf("Bad blocks check: FAILED (%d bad blocks found)\n", bad_block_errors);
        return false;
    } else {
        printf("Bad blocks check: PASSED\n");
        return true;
    }
}

void print_fsck_results() {
    printf("\nFSCK Results Summary:\n");
    printf("--------------------\n");
    printf("Superblock errors: %d\n", superblock_errors);
    printf("Inode bitmap errors: %d\n", inode_bitmap_errors);
    printf("Data bitmap errors: %d\n", data_bitmap_errors);
    printf("Duplicate block errors: %d\n", duplicate_block_errors);
    printf("Bad block errors: %d\n", bad_block_errors);
    int total_errors = superblock_errors + inode_bitmap_errors + data_bitmap_errors +
                      duplicate_block_errors + bad_block_errors;
    if (total_errors == 0) {
        printf("\nFSCK completed successfully. File system is consistent.\n");
    } else {
        printf("\nFSCK found %d errors.\n", total_errors);
    }
}

void fix_block_reference(uint32_t *block_ptr, int inode_num, const char *block_type) {
    
    if (*block_ptr != 0 && (*block_ptr < DATA_BLOCK_START || *block_ptr >= TOTAL_BLOCKS)) {
        printf("Fixed bad block: Inode %d, %s block %u (invalid range)\n", inode_num, block_type, *block_ptr);
        *block_ptr = 0; // Clear the invalid reference
    }
}

// Fix all block pointers 
void fix_all_inode_blocks(inode_t *inode, int inode_num) {
    fix_block_reference(&inode->direct_block, inode_num, "direct");
    fix_block_reference(&inode->single_indirect, inode_num, "single indirect");
    fix_block_reference(&inode->double_indirect, inode_num, "double indirect");
    fix_block_reference(&inode->triple_indirect, inode_num, "triple indirect");
}


void fix_errors() {
    // Fix superblock if needed
    if (superblock_errors > 0) {
        printf("Fixing superblock...\n");
        superblock.magic = VSFS_MAGIC;
        superblock.block_size = BLOCK_SIZE;
        superblock.total_blocks = TOTAL_BLOCKS;
        superblock.inode_bitmap_block = INODE_BITMAP_BLOCK_NUM;
        superblock.data_bitmap_block = DATA_BITMAP_BLOCK_NUM;
        superblock.inode_table_block = INODE_TABLE_START_BLOCK;
        superblock.data_block_start = DATA_BLOCK_START;
        superblock.inode_size = INODE_SIZE;
        superblock.inode_count = INODE_COUNT;
        
        if (!write_block(SUPERBLOCK_BLOCK_NUM, &superblock)) {
            printf("Error writing superblock\n");
        }
    }
    
    // Fix inode bitmap if needed
    if (inode_bitmap_errors > 0) {
        printf("Fixing inode bitmap...\n");
        
        // Reset inode bitmap
        memset(inode_bitmap, 0, BLOCK_SIZE);
        
        // Mark inodes as used based on their validity
        for (int i = 0; i < INODE_COUNT; i++) {
            inode_t inode;
            int block_num = INODE_TABLE_START_BLOCK + (i * INODE_SIZE) / BLOCK_SIZE;
            int offset = (i * INODE_SIZE) % BLOCK_SIZE;
            
            uint8_t block[BLOCK_SIZE];
            if (!read_block(block_num, block)) {
                printf("Error reading inode table block %d\n", block_num);
                continue;
            }
            
            memcpy(&inode, block + offset, sizeof(inode_t));
            
            // Mark valid inodes as used in the bitmap
            if (is_valid_inode(&inode)) {
                set_bit(inode_bitmap, i);
            }
        }
        
        // Write updated inode bitmap
        if (!write_block(INODE_BITMAP_BLOCK_NUM, inode_bitmap)) {
            printf("Error writing inode bitmap\n");
        }
    }
    
    // Fix data bitmap if needed
    if (data_bitmap_errors > 0) {
        printf("Fixing data bitmap...\n");
        
        // Reset data bitmap
        memset(data_bitmap, 0, BLOCK_SIZE);
        
        // Mark blocks as used based on valid inodes
        for (int i = DATA_BLOCK_START; i < TOTAL_BLOCKS; i++) {
            if (used_blocks[i]) {
                set_bit(data_bitmap, i - DATA_BLOCK_START);
            }
        }
        
        // Write updated data bitmap
        if (!write_block(DATA_BITMAP_BLOCK_NUM, data_bitmap)) {
            printf("Error writing data bitmap\n");
        }
    }
    
    
    if (duplicate_block_errors > 0) {
        printf("Fixing duplicate blocks...\n");
        
        // Creating a map to track which inode first used each block
        int first_user_inode[TOTAL_BLOCKS];
        memset(first_user_inode, -1, sizeof(first_user_inode));
        
        
        for (int i = 0; i < INODE_COUNT; i++) {
            inode_t inode;
            int block_num = INODE_TABLE_START_BLOCK + (i * INODE_SIZE) / BLOCK_SIZE;
            int offset = (i * INODE_SIZE) % BLOCK_SIZE;
            
            uint8_t block[BLOCK_SIZE];
            if (!read_block(block_num, block)) {
                continue;
            }
            
            memcpy(&inode, block + offset, sizeof(inode_t));
            
            
            if (!is_valid_inode(&inode)) {
                continue;
            }
            
            // Check direct block
            if (inode.direct_block >= DATA_BLOCK_START && inode.direct_block < TOTAL_BLOCKS) {
                if (first_user_inode[inode.direct_block] == -1) {
                    first_user_inode[inode.direct_block] = i;
                }
            }
            
        
        }
        
        // Now fix duplicates for each inode
        for (int i = 0; i < INODE_COUNT; i++) {
            inode_t inode;
            int block_num = INODE_TABLE_START_BLOCK + (i * INODE_SIZE) / BLOCK_SIZE;
            int offset = (i * INODE_SIZE) % BLOCK_SIZE;
            
            uint8_t inode_block[BLOCK_SIZE];
            if (!read_block(block_num, inode_block)) {
                continue;
            }
            
            memcpy(&inode, inode_block + offset, sizeof(inode_t));
            
            // Skip invalid inodes
            if (!is_valid_inode(&inode)) {
                continue;
            }
            
            bool inode_modified = false;
            
            // Fix direct block if it's duplicated
            if (inode.direct_block >= DATA_BLOCK_START && inode.direct_block < TOTAL_BLOCKS) {
                if (duplicated_blocks[inode.direct_block] && 
                    first_user_inode[inode.direct_block] != i) {
                    
                    // Allocate a new block for this inode
                    int new_block = allocate_new_data_block();
                    if (new_block != -1) {
                        // Copy data from old block to new block
                        uint8_t buffer[BLOCK_SIZE];
                        if (read_block(inode.direct_block, buffer) && 
                            write_block(new_block, buffer)) {
                            
                            printf("Fixed duplicate: Inode %d, direct block %d (*). %d\n", 
                                   i, inode.direct_block, new_block);
                            
                            // Update the inode
                            inode.direct_block = new_block;
                            inode_modified = true;
                            
                            // Mark the new block as used
                            set_bit(data_bitmap, new_block - DATA_BLOCK_START);
                            used_blocks[new_block] = true;
                        }
                    }
                }
            }
            
            // Modified inode
            if (inode_modified) {
                memcpy(inode_block + offset, &inode, sizeof(inode_t));
                write_block(block_num, inode_block);
            }
        }
        
        // Updated data bitmap
        if (!write_block(DATA_BITMAP_BLOCK_NUM, data_bitmap)) {
            printf("Error writing data bitmap\n");
        }
    }
    
    // Fix bad blocks
    if (bad_block_errors > 0) {
    printf("Fixing bad blocks...\n");
    for (int i = 0; i < INODE_COUNT; i++) {
        inode_t inode;
        int block_num = INODE_TABLE_START_BLOCK + (i * INODE_SIZE) / BLOCK_SIZE;
        int offset = (i * INODE_SIZE) % BLOCK_SIZE;
        uint8_t inode_block[BLOCK_SIZE];
        if (!read_block(block_num, inode_block)) {
            continue;
        }
        memcpy(&inode, inode_block + offset, sizeof(inode_t));
        if (!is_valid_inode(&inode)) {
            continue;
        }

        // Fix all block pointers 
        fix_all_inode_blocks(&inode, i);
        memcpy(inode_block + offset, &inode, sizeof(inode_t));
        write_block(block_num, inode_block);
    }
}
    //  updated data bitmap
    if (data_bitmap_errors > 0 || duplicate_block_errors > 0 || bad_block_errors > 0) {
        if (!write_block(DATA_BITMAP_BLOCK_NUM, data_bitmap)) {
            printf("Error writing data bitmap\n");
        }
    }
}
int allocate_new_data_block() {
    for (int i = 0; i < TOTAL_BLOCKS - DATA_BLOCK_START; i++) {
        if (!is_used_bit(data_bitmap, i)) {

            // Free block found
            int block_num = i + DATA_BLOCK_START;
            set_bit(data_bitmap, i);
            used_blocks[block_num] = true;
            set_bit(data_bitmap, i);
            used_blocks[block_num] = true;
            
            // Clear the block contents
            uint8_t zeros[BLOCK_SIZE] = {0};
            write_block(block_num, zeros);
            
            return block_num;
        }
    }
    return -1;  // Kono free block nei
}