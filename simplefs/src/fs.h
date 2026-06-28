#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stdio.h>

/*Disk Layout Constants*/
/*
 * fs.h
 *
 * Public definitions of SimpleFS.
 *
 * Contains:
 *
 *  - Disk layout constants
 *  - Filesystem structures
 *  - In-memory filesystem handle
 *  - Public API functions
 *
 * fs.c contains the implementation.
 * main.c uses only this interface.
 */


/*
 * Filesystem geometry
 *
 * Disk:
 *
 *   4096 blocks
 *   512 bytes each
 *
 * Total size:
 *
 *   4096 * 512 = 2MB
 */
#define BLOCK_SIZE 512
#define DISK_BLOCKS 4096

/*
 * Inode system
 *
 * The filesystem supports:
 *
 *   256 files/directories
 *
 * Every object has exactly one inode.
 */
#define INODE_COUNT 256

/*
 * Each inode can store
 * 12 direct block pointers.
 *
 * Therefore maximum file size:
 *
 * 12 * 512 = 6144 bytes
 */
#define DIRECT_BLOCKS 12

/*
 * Maximum filename length
 */
#define MAX_FILENAME 60
#define DIR_ENTRIES 16

/*
 * Special inode values
 */
#define ROOT_INODE 0

/*
 * Directory entry value meaning:
 *
 * "this slot is empty"
 */
#define DIR_ENTRY_FREE 0xFFFFFFFFu

/*Block offsets (block numbers)*/
/*
 * Disk layout:
 *
 * Block 0:
 *      Superblock
 *
 * Block 1:
 *      inode bitmap
 *
 * Block 2:
 *      block bitmap
 *
 * Blocks 3-10:
 *      inode table
 *
 * Remaining blocks:
 *      file and directory data
 */
#define SUPERBLOCK_BLOCK 0
#define INODE_BITMAP_BLK 1
#define BLOCK_BITMAP_BLK 2
#define INODE_TABLE_BLK 3
#define INODE_TABLE_BLKS 8
#define DATA_START_BLK (INODE_TABLE_BLK + INODE_TABLE_BLKS)

/*File types*/
#define FT_NONE  0
#define FT_FILE  1
#define FT_DIR   2

/*Magic number for format detection*/
#define FS_MAGIC 0x53464653 /*"SFFS"*/

/*Superblock  (stored at block 0) Holds global metadata about the volume.*/
typedef struct 
{
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t data_start;
    uint8_t _pad[BLOCK_SIZE - 7*4];
} __attribute__((packed)) Superblock;

/*Inode  (64 bytes each) One inode per file or directory.*/
typedef struct 
{
    uint8_t type;
    uint8_t _pad0[3];
    uint32_t size;
    uint32_t blocks_used;
    uint32_t direct[DIRECT_BLOCKS];
    uint32_t _pad1[2]; 
} __attribute__((packed)) Inode; 

/* static check at compile time */
_Static_assert(sizeof(Inode) == 68, "Inode must be 68 bytes");

/*Directory Entry  (64 bytes each - 8 entries per 512-byte block)*/
typedef struct 
{
    uint32_t inode_num;
    char name[MAX_FILENAME];
    uint8_t type;
    uint8_t _pad[3];
} __attribute__((packed)) DirEntry;

_Static_assert(sizeof(DirEntry) == 68, "DirEntry must be 68 bytes");

/*File-system handle One of these lives in memory while the FS is mounted.*/
typedef struct 
{
    FILE *disk;
    Superblock sb;
    uint8_t inode_bitmap[INODE_COUNT / 8];
    uint8_t block_bitmap[DISK_BLOCKS / 8];
} FS;

/*Public API*/

/*Lifecycle*/
int fs_format(const char *path);
int fs_mount(FS *fs, const char *path);
void fs_unmount(FS *fs);

/*Block & inode allocation*/
int fs_alloc_block(FS *fs);
void fs_free_block(FS *fs, int block);
int fs_alloc_inode(FS *fs);
void fs_free_inode(FS *fs, int ino);

/*Raw I/O*/
int fs_read_block(FS *fs, int block, void *buf);
int fs_write_block(FS *fs, int block, const void *buf);
int fs_read_inode(FS *fs, int ino, Inode *out);
int fs_write_inode(FS *fs, int ino, const Inode *in);

/*High-level operations*/
int fs_mkdir(FS *fs, int parent_ino, const char *name);
int fs_create(FS *fs, int parent_ino, const char *name);
int fs_lookup(FS *fs, int dir_ino, const char *name);
int fs_write_file(FS *fs, int ino, const uint8_t *data, uint32_t len);
int fs_read_file(FS *fs, int ino, uint8_t *buf, uint32_t len);
int fs_unlink(FS *fs, int parent_ino, const char *name);
int fs_list_dir(FS *fs, int dir_ino);
int fs_resolve_path(FS *fs, const char *path, int cwd_ino);

/*Persistence helpers*/
int fs_flush_superblock(FS *fs);
int fs_flush_bitmaps(FS *fs);

#endif /*FS_H*/
