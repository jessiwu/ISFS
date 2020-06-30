#ifndef CONFIG_H
#define CONFIG_H

#include <time.h>

#define BLK_SIZE 512                // block size in bytes
#define PAGE_SIZE 4096              // page size in bytes
#define INODE_SIZE 256              // inode size in bytes
#define SUPERBLK_SIZE 4096          // superblock size in bytes
#define INODE_COUNT 196608          // the total number of inodes
#define BLK_COUNT 156864            // the total number of blocks (in blocks)
#define DIRECT_BLOCK_PTR_NUM 12     // the number of direct data block pointers for one inode
#define INDIRECT_BLOCK_PTR_NUM 128  // the number of data blocks which one indirect data block points to
#define DIR_ENTRY_NAME_LEN 256      // the maximum length of name of this directory entry
#define ROOT_DIR_INODE_NUM 2        // the inode number of the root directory

struct super_block {
    int blk_size;
    int inode_size;
    int blk_count;
    int inode_count;
    int free_blk_count;
    int free_inode_count;
    int root_dir_inode_num;
};

struct block_bitmap {
    int blk_bitmap[BLK_COUNT];
};

struct inode_bitmap {
    int inode_bitmap[INODE_COUNT];
};

struct data_block {
    char buf[BLK_SIZE];  // 1 data block = 512 bytes
};

struct inode {
    struct timespec atime;
    struct timespec mtime;
    struct timespec ctime;

    struct data_block* direct_blk_ptr[DIRECT_BLOCK_PTR_NUM];
    struct data_block* single_indirect_ptr;
    struct data_block* double_indirect_ptr;
    struct data_block* triple_indirect_ptr;

    int direct[DIRECT_BLOCK_PTR_NUM];
    int single_indirect;
};

/*
// struct data_block_table {
//     struct data_block data_block_table[BLK_COUNT];  // 1 data block table contains [BLK_COUNT:156864] data blocks
// };

// struct inode_table {
//     struct inode inode_table[INODE_COUNT];  // 1 inode table contains [INODE_COUNT:196608] inodes
// };

// struct data_block data_block_table[BLK_COUNT];  // 1 data block table contains [BLK_COUNT:156864] data blocks
*/

struct inode inode_table[INODE_COUNT];          // 1 inode table contains [INODE_COUNT:196608] inodes
struct data_block data_blk_table[BLK_COUNT];    // 1 data block table contains [BLK_COUNT:156864] data blocks

enum FILE_TYPE {
    UNKNOWN=0,
    REGULAR=1,
    DIRECTORY=2,
};

struct directory_entry {            // 1 directory entry size = 4+4+256 = 264 bytes
    int inode_num;                  // the inode number of this directory
    enum FILE_TYPE file_type;       // the file type of this directory entry
    char name[DIR_ENTRY_NAME_LEN];  // the name of this directory entry
    char waste[248];                // the name of this directory entry

    //int rec_len;
    //int name_len;                   // the length of this directory entry name
};

struct directory {
    struct directory_entry* directory_entry_table_ptr; 
};

struct indirect_data_block {
    int idx[INDIRECT_BLOCK_PTR_NUM];
};

#endif