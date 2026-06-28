#include "fs.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*Internal helpers*/
/*
 * fs.c
 *
 * Core implementation of the SimpleFS filesystem.
 *
 * Responsibilities:
 *
 *  - Manage the virtual disk image
 *  - Allocate/free blocks and inodes
 *  - Read/write inode structures
 *  - Create/remove files and directories
 *  - Handle directory entries
 *
 *
 * Disk layout:
 *
 * Block 0:
 *      Superblock
 *
 * Block 1:
 *      Inode bitmap
 *
 * Block 2:
 *      Block bitmap
 *
 * Following blocks:
 *      Inode table
 *
 * Remaining blocks:
 *      File and directory data
 */

/*Byte offset of block N inside the disk image*/
static inline long block_offset(int n) 
{
    return (long)n * BLOCK_SIZE;
}

/*
 * Bitmap helpers
 *
 * The filesystem uses bit arrays:
 *
 * 0 = free
 * 1 = used
 *
 * Each bit represents:
 *   - an inode
 *   - a disk block
 */
static void bitmap_set(uint8_t *bm, int idx) 
{
    bm[idx / 8] |= (1u << (idx % 8));
}

static void bitmap_clear(uint8_t *bm, int idx) 
{
    bm[idx / 8] &= ~(1u << (idx % 8));
}

static int bitmap_test(const uint8_t *bm, int idx) 
{
    return (bm[idx / 8] >> (idx % 8)) & 1;
}

/*Find the first 0-bit in bm[0..len-1] bits; returns -1 if all set*/
static int bitmap_first_free(const uint8_t *bm, int len) 
{
    for (int i = 0; i < len; i++) 
    {
        if (!bitmap_test(bm, i)) return i;
    }
    return -1;
}

/*Raw block I/O*/
int fs_read_block(FS *fs, int block, void *buf) 
{
    if (block < 0 || block >= DISK_BLOCKS) return -1;

    if (fseek(fs->disk, block_offset(block), SEEK_SET) != 0) return -1;

    if (fread(buf, BLOCK_SIZE, 1, fs->disk) != 1) return -1;
    return 0;
}

int fs_write_block(FS *fs, int block, const void *buf) 
{
    if (block < 0 || block >= DISK_BLOCKS) return -1;

    if (fseek(fs->disk, block_offset(block), SEEK_SET) != 0) return -1;

    if (fwrite(buf, BLOCK_SIZE, 1, fs->disk) != 1) return -1;
    fflush(fs->disk);
    return 0;
}

/*Inode I/O  (inodes are packed into INODE_TABLE_BLKS blocks)*/

/*How many inodes fit in a single block*/
#define INODES_PER_BLOCK  (BLOCK_SIZE / sizeof(Inode))

int fs_read_inode(FS *fs, int ino, Inode *out) 
{
    if (ino < 0 || ino >= (int)INODE_COUNT) return -1;
    int block   = INODE_TABLE_BLK + (ino / INODES_PER_BLOCK);
    int offset  = (ino % INODES_PER_BLOCK) * sizeof(Inode);
    uint8_t buf[BLOCK_SIZE];

    if (fs_read_block(fs, block, buf) != 0) return -1;
    memcpy(out, buf + offset, sizeof(Inode));
    return 0;
}

int fs_write_inode(FS *fs, int ino, const Inode *in) 
{
    if (ino < 0 || ino >= (int)INODE_COUNT) return -1;
    int block   = INODE_TABLE_BLK + (ino / INODES_PER_BLOCK);
    int offset  = (ino % INODES_PER_BLOCK) * sizeof(Inode);
    uint8_t buf[BLOCK_SIZE];

    if (fs_read_block(fs, block, buf) != 0) return -1;
    memcpy(buf + offset, in, sizeof(Inode));
    return fs_write_block(fs, block, buf);
}

/*Allocation / deallocation*/
/*
 * Allocate a free data block.
 *
 * Algorithm:
 *
 * 1. Search block bitmap
 * 2. Find first free block
 * 3. Mark it used
 * 4. Decrease free block counter
 * 5. Clear block contents
 *
 * Returns:
 *   block number
 *
 * Returns -1 if disk is full.
 */
int fs_alloc_block(FS *fs) 
{
    /*search only in the data region*/
    int total = DISK_BLOCKS;
    for (int i = DATA_START_BLK; i < total; i++) 
    {
        if (!bitmap_test(fs->block_bitmap, i)) 
        {
            bitmap_set(fs->block_bitmap, i);
            fs->sb.free_blocks--;

            /*zero-fill the new block*/
            uint8_t zero[BLOCK_SIZE] = {0};
            fs_write_block(fs, i, zero);
            return i;
        }
    }
    return -1; /*disk full*/
}

void fs_free_block(FS *fs, int block) 
{
    if (block < DATA_START_BLK || block >= DISK_BLOCKS) return;
    if (bitmap_test(fs->block_bitmap, block)) {
        bitmap_clear(fs->block_bitmap, block);
        fs->sb.free_blocks++;
    }
}

int fs_alloc_inode(FS *fs) 
{
    int ino = bitmap_first_free(fs->inode_bitmap, INODE_COUNT);
    if (ino < 0) return -1;
    bitmap_set(fs->inode_bitmap, ino);
    fs->sb.free_inodes--;
    return ino;
}

void fs_free_inode(FS *fs, int ino) 
{
    if (ino < 0 || ino >= INODE_COUNT) return;
    bitmap_clear(fs->inode_bitmap, ino);
    fs->sb.free_inodes++;
}

/*Superblock & bitmap persistence*/
int fs_flush_superblock(FS *fs) 
{
    uint8_t buf[BLOCK_SIZE] = {0};
    memcpy(buf, &fs->sb, sizeof(Superblock));
    return fs_write_block(fs, SUPERBLOCK_BLOCK, buf);
}

int fs_flush_bitmaps(FS *fs) 
{
    uint8_t buf[BLOCK_SIZE] = {0};

    /*inode bitmap → block 1*/
    memcpy(buf, fs->inode_bitmap, sizeof(fs->inode_bitmap));
    if (fs_write_block(fs, INODE_BITMAP_BLK, buf) != 0) return -1;

    /*block bitmap → block 2*/
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, fs->block_bitmap, sizeof(fs->block_bitmap));
    return fs_write_block(fs, BLOCK_BITMAP_BLK, buf);
}

/*Format & mount / unmount*/
/*
 * Create a new filesystem.
 *
 * This completely destroys previous data.
 *
 * Steps:
 *
 * 1. Create empty disk image
 * 2. Write superblock
 * 3. Initialize inode bitmap
 * 4. Initialize block bitmap
 * 5. Create root inode
 * 6. Create root directory
 *
 */
int fs_format(const char *path) 
{
    /* Create / overwrite disk image with all-zero bytes */
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        printf("DEBUG: failed creating disk at: %s\n", path);
        perror("fopen");
        return -1;
    }
    if (!f) { perror("fopen"); return -1; }

    uint8_t zero[BLOCK_SIZE] = {0};
    for (int i = 0; i < DISK_BLOCKS; i++) 
    {
        if (fwrite(zero, BLOCK_SIZE, 1, f) != 1) {
            fclose(f); return -1;
        }
    }

    /*Superblock*/
    Superblock sb = {0};
    sb.magic = FS_MAGIC;
    sb.block_size = BLOCK_SIZE;
    sb.total_blocks = DISK_BLOCKS;
    sb.inode_count = INODE_COUNT;
    sb.free_blocks = DISK_BLOCKS - DATA_START_BLK;
    sb.free_inodes = INODE_COUNT - 1; /* root takes one */
    sb.data_start = DATA_START_BLK;

    memset(zero, 0, BLOCK_SIZE);
    memcpy(zero, &sb, sizeof(sb));
    fseek(f, block_offset(SUPERBLOCK_BLOCK), SEEK_SET);
    fwrite(zero, BLOCK_SIZE, 1, f);

    /*Inode bitmap - mark inode 0 (root) as used*/
    memset(zero, 0, BLOCK_SIZE);
    zero[0] = 0x01; /* bit 0 set */
    fseek(f, block_offset(INODE_BITMAP_BLK), SEEK_SET);
    fwrite(zero, BLOCK_SIZE, 1, f);

    /*Block bitmap - mark metadata blocks as used*/
    memset(zero, 0, BLOCK_SIZE);
    for (int i = 0; i < DATA_START_BLK; i++) {
        zero[i / 8] |= (1u << (i % 8));
    }

    /*also mark the single data block we'll give root's dir*/
    int root_data = DATA_START_BLK;
    zero[root_data / 8] |= (1u << (root_data % 8));
    fseek(f, block_offset(BLOCK_BITMAP_BLK), SEEK_SET);
    fwrite(zero, BLOCK_SIZE, 1, f);

    /*Root inode (inode 0)*/
    uint8_t inode_block[BLOCK_SIZE] = {0};
    Inode root = {0};
    root.type = FT_DIR;
    root.size = BLOCK_SIZE;
    root.blocks_used = 1;
    root.direct[0] = root_data;
    memcpy(inode_block, &root, sizeof(Inode));
    fseek(f, block_offset(INODE_TABLE_BLK), SEEK_SET);
    fwrite(inode_block, BLOCK_SIZE, 1, f);

    /*Root directory block (empty . and .. entries)*/
    uint8_t dir_block[BLOCK_SIZE];

    /* Fill all slots with FREE sentinel */
    DirEntry empty_e = {0}; empty_e.inode_num = 0xFFFFFFFFu;
    for (int i = 0; i < (int)(BLOCK_SIZE / sizeof(DirEntry)); i++)
        memcpy(dir_block + i * sizeof(DirEntry), &empty_e, sizeof(DirEntry));

    DirEntry *dot    = (DirEntry *)(dir_block + 0);
    DirEntry *dotdot = (DirEntry *)(dir_block + sizeof(DirEntry));
    dot->inode_num = 0; strncpy(dot->name, ".",  MAX_FILENAME); dot->type = FT_DIR;
    dotdot->inode_num = 0; strncpy(dotdot->name, "..", MAX_FILENAME); dotdot->type = FT_DIR;
    fseek(f, block_offset(root_data), SEEK_SET);
    fwrite(dir_block, BLOCK_SIZE, 1, f);

    fclose(f);
    return 0;
}

int fs_mount(FS *fs, const char *path) 
{
    fs->disk = fopen(path, "r+b");
    if (!fs->disk) { perror("fopen"); return -1; }

    /* Read superblock */
    uint8_t buf[BLOCK_SIZE];
    if (fs_read_block(fs, SUPERBLOCK_BLOCK, buf) != 0) return -1;
    memcpy(&fs->sb, buf, sizeof(Superblock));

    if (fs->sb.magic != FS_MAGIC) 
    {
        fprintf(stderr, "fs_mount: bad magic — not a SimpleFS volume\n");
        fclose(fs->disk); return -1;
    }

    /* Read bitmaps */
    if (fs_read_block(fs, INODE_BITMAP_BLK, buf) != 0) return -1;
    memcpy(fs->inode_bitmap, buf, sizeof(fs->inode_bitmap));

    if (fs_read_block(fs, BLOCK_BITMAP_BLK, buf) != 0) return -1;
    memcpy(fs->block_bitmap, buf, sizeof(fs->block_bitmap));

    return 0;
}

void fs_unmount(FS *fs) 
{
    if (!fs->disk) return;
    fs_flush_superblock(fs);
    fs_flush_bitmaps(fs);
    fclose(fs->disk);
    fs->disk = NULL;
}

/*Directory helpers*/
#define ENTRIES_PER_BLOCK  (BLOCK_SIZE / sizeof(DirEntry))

/*Find a free slot in a directory inode; returns (block, slot-in-block). Creates a new block if all existing blocks are full.*/
static int dir_find_free_slot(FS *fs, Inode *dir, int *out_block, int *out_slot) 
{
    for (int bi = 0; bi < (int)dir->blocks_used; bi++) 
    {
        int blk = dir->direct[bi];
        uint8_t buf[BLOCK_SIZE];

        if (fs_read_block(fs, blk, buf) != 0) return -1;
        DirEntry *entries = (DirEntry *)buf;

        for (int si = 0; si < (int)ENTRIES_PER_BLOCK; si++) 
        {
            if (entries[si].inode_num == DIR_ENTRY_FREE) {
                *out_block = blk;
                *out_slot  = si;
                return 0;
            }
        }
    }

    /*No free slot - allocate a new data block for the directory*/
    if (dir->blocks_used >= DIRECT_BLOCKS) return -1; /*dir too large*/
    int new_blk = fs_alloc_block(fs);
    if (new_blk < 0) return -1;

    /*Initialize all entries in the new block to FREE sentinel*/
    uint8_t new_buf[BLOCK_SIZE];
    DirEntry empty_e = {0}; empty_e.inode_num = DIR_ENTRY_FREE;

    for (int i = 0; i < (int)(BLOCK_SIZE / sizeof(DirEntry)); i++)
        memcpy(new_buf + i * sizeof(DirEntry), &empty_e, sizeof(DirEntry));

    fs_write_block(fs, new_blk, new_buf);

    dir->direct[dir->blocks_used] = new_blk;
    dir->blocks_used++;
    dir->size += BLOCK_SIZE;
    *out_block = new_blk;
    *out_slot  = 0;
    return 0;
}

/*Add a named entry to a directory*/
static int dir_add_entry(FS *fs, int dir_ino, const char *name, int child_ino, uint8_t type, Inode *dir) 
{
    int blk, slot;
    if (dir_find_free_slot(fs, dir, &blk, &slot) != 0) return -1;

    uint8_t buf[BLOCK_SIZE];
    if (fs_read_block(fs, blk, buf) != 0) return -1;
    DirEntry *e = (DirEntry *)buf + slot;
    e->inode_num = (uint32_t)child_ino;
    strncpy(e->name, name, MAX_FILENAME - 1);
    e->name[MAX_FILENAME - 1] = '\0';
    e->type = type;

    if (fs_write_block(fs, blk, buf) != 0) return -1;
    if (fs_write_inode(fs, dir_ino, dir)  != 0) return -1;

    return 0;
}

/*High-level operations*/
int fs_lookup(FS *fs, int dir_ino, const char *name) 
{
    Inode dir;
    if (fs_read_inode(fs, dir_ino, &dir) != 0) return -1;
    if (dir.type != FT_DIR) return -1;

    for (int bi = 0; bi < (int)dir.blocks_used; bi++) {
        uint8_t buf[BLOCK_SIZE];
        if (fs_read_block(fs, dir.direct[bi], buf) != 0) return -1;
        DirEntry *entries = (DirEntry *)buf;
        for (int si = 0; si < (int)ENTRIES_PER_BLOCK; si++) {
            if (entries[si].inode_num != DIR_ENTRY_FREE &&
                strncmp(entries[si].name, name, MAX_FILENAME) == 0) {
                return (int)entries[si].inode_num;
            }
        }
    }
    return -1; /*not found*/
}

int fs_resolve_path(FS *fs, const char *path, int cwd_ino) 
{
    if (!path || path[0] == '\0') return cwd_ino;

    int cur = (path[0] == '/') ? ROOT_INODE : cwd_ino;
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp)-1] = '\0';

    char *tok = strtok(tmp, "/");
    while (tok) 
    {
        if (strcmp(tok, ".") == 0) { tok = strtok(NULL, "/"); continue; }
        cur = fs_lookup(fs, cur, tok);
        if (cur < 0) return -1;
        tok = strtok(NULL, "/");
    }
    return cur;
}

int fs_mkdir(FS *fs, int parent_ino, const char *name) 
{
    /* Ensure name doesn't already exist */
    if (fs_lookup(fs, parent_ino, name) >= 0) 
    {
        fprintf(stderr, "mkdir: '%s' already exists\n", name);
        return -1;
    }

    int ino = fs_alloc_inode(fs);
    if (ino < 0) { fprintf(stderr, "mkdir: no free inodes\n"); return -1; }

    int data_blk = fs_alloc_block(fs);
    if (data_blk < 0) { fs_free_inode(fs, ino); return -1; }

    /*Initialise inode*/
    Inode dir = {0};
    dir.type = FT_DIR;
    dir.size = BLOCK_SIZE;
    dir.blocks_used = 1;
    dir.direct[0] = data_blk;

    if (fs_write_inode(fs, ino, &dir) != 0) return -1;

    /*Initialise directory block with . and ..*/
    uint8_t buf[BLOCK_SIZE];
    DirEntry empty_e = {0}; empty_e.inode_num = DIR_ENTRY_FREE;
    for (int i = 0; i < (int)(BLOCK_SIZE / sizeof(DirEntry)); i++)
        memcpy(buf + i * sizeof(DirEntry), &empty_e, sizeof(DirEntry));

    DirEntry *dot    = (DirEntry *)(buf + 0);
    DirEntry *dotdot = (DirEntry *)(buf + sizeof(DirEntry));
    dot->inode_num    = ino;         strncpy(dot->name,    ".",  MAX_FILENAME); dot->type    = FT_DIR;
    dotdot->inode_num = parent_ino;  strncpy(dotdot->name, "..", MAX_FILENAME); dotdot->type = FT_DIR;

    if (fs_write_block(fs, data_blk, buf) != 0) return -1;

    /*Add entry to parent*/
    Inode parent;
    if (fs_read_inode(fs, parent_ino, &parent) != 0) return -1;
    if (dir_add_entry(fs, parent_ino, name, ino, FT_DIR, &parent) != 0) return -1;

    fs_flush_superblock(fs);
    fs_flush_bitmaps(fs);
    return ino;
}

int fs_create(FS *fs, int parent_ino, const char *name) 
{
    if (fs_lookup(fs, parent_ino, name) >= 0) 
    {
        fprintf(stderr, "create: '%s' already exists\n", name);
        return -1;
    }

    int ino = fs_alloc_inode(fs);
    if (ino < 0) { fprintf(stderr, "create: no free inodes\n"); return -1; }

    Inode f = {0};
    f.type = FT_FILE;
    f.size = 0;
    f.blocks_used = 0;
    if (fs_write_inode(fs, ino, &f) != 0) return -1;

    Inode parent;
    if (fs_read_inode(fs, parent_ino, &parent) != 0) return -1;
    if (dir_add_entry(fs, parent_ino, name, ino, FT_FILE, &parent) != 0) return -1;

    fs_flush_superblock(fs);
    fs_flush_bitmaps(fs);

    return ino;
}

int fs_write_file(FS *fs, int ino, const uint8_t *data, uint32_t len) 
{
    Inode f;
    if (fs_read_inode(fs, ino, &f) != 0) return -1;
    if (f.type != FT_FILE) return -1;

    /*Free all existing blocks first (overwrite semantics)*/
    for (uint32_t i = 0; i < f.blocks_used; i++) 
    {
        fs_free_block(fs, f.direct[i]);
        f.direct[i] = 0;
    }

    f.blocks_used = 0;
    f.size = 0;

    uint32_t written = 0;
    while (written < len) 
    {
        if (f.blocks_used >= DIRECT_BLOCKS) {
            fprintf(stderr, "write_file: file too large (max %d blocks)\n", DIRECT_BLOCKS);
            break;
        }

        int blk = fs_alloc_block(fs);
        if (blk < 0) { fprintf(stderr, "write_file: disk full\n"); break; }

        uint32_t chunk = (len - written > BLOCK_SIZE) ? BLOCK_SIZE : (len - written);
        uint8_t buf[BLOCK_SIZE] = {0};
        memcpy(buf, data + written, chunk);

        if (fs_write_block(fs, blk, buf) != 0) break;

        f.direct[f.blocks_used++] = blk;
        written += chunk;
    }

    f.size = written;
    fs_write_inode(fs, ino, &f);
    fs_flush_superblock(fs);
    fs_flush_bitmaps(fs);
    return (int)written;
}

int fs_read_file(FS *fs, int ino, uint8_t *buf, uint32_t len) 
{
    Inode f;
    if (fs_read_inode(fs, ino, &f) != 0) return -1;
    if (f.type != FT_FILE) return -1;

    uint32_t to_read = (len < f.size) ? len : f.size;
    uint32_t done    = 0;

    for (uint32_t bi = 0; bi < f.blocks_used && done < to_read; bi++) {
        uint8_t blk_buf[BLOCK_SIZE];
        if (fs_read_block(fs, f.direct[bi], blk_buf) != 0) return -1;
        uint32_t chunk = to_read - done;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;
        memcpy(buf + done, blk_buf, chunk);
        done += chunk;
    }
    return (int)done;
}

/*
 * Remove a file or directory.
 *
 * Steps:
 *
 * 1. Find directory entry
 * 2. Check if directory is empty
 * 3. Free data blocks
 * 4. Free inode
 * 5. Remove directory entry
 *
 */
int fs_unlink(FS *fs, int parent_ino, const char *name) 
{
    /*Find the entry in parent*/
    Inode parent;
    if (fs_read_inode(fs, parent_ino, &parent) != 0) return -1;

    for (int bi = 0; bi < (int)parent.blocks_used; bi++) 
    {
        uint8_t buf[BLOCK_SIZE];
        int blk = parent.direct[bi];
        if (fs_read_block(fs, blk, buf) != 0) return -1;
        DirEntry *entries = (DirEntry *)buf;
        for (int si = 0; si < (int)ENTRIES_PER_BLOCK; si++) {
            if (entries[si].inode_num != DIR_ENTRY_FREE &&
                strncmp(entries[si].name, name, MAX_FILENAME) == 0) 
                {

                int child_ino  = entries[si].inode_num;
                int child_type = entries[si].type;

                /*Refuse to remove non-empty directories*/
                if (child_type == FT_DIR) {
                    Inode child;
                    fs_read_inode(fs, child_ino, &child);
                    /*count real entries (skip . and ..)*/
                    int count = 0;
                    for (int bi2 = 0; bi2 < (int)child.blocks_used; bi2++) 
                    {
                        uint8_t b2[BLOCK_SIZE];
                        fs_read_block(fs, child.direct[bi2], b2);
                        DirEntry *e2 = (DirEntry *)b2;

                        for (int si2 = 0; si2 < (int)ENTRIES_PER_BLOCK; si2++) 
                        {
                            if (e2[si2].inode_num != DIR_ENTRY_FREE &&
                                strcmp(e2[si2].name, ".") != 0 &&
                                strcmp(e2[si2].name, "..") != 0)
                                count++;
                        }
                    }
                    if (count > 0) {
                        fprintf(stderr, "rm: '%s' is a non-empty directory\n", name);
                        return -1;
                    }
                    /*free dir data blocks*/
                    Inode child2;
                    fs_read_inode(fs, child_ino, &child2);
                    for (uint32_t k = 0; k < child2.blocks_used; k++)
                        fs_free_block(fs, child2.direct[k]);
                }
                else 
                {
                    /*free file data blocks*/
                    Inode child2;
                    fs_read_inode(fs, child_ino, &child2);
                    for (uint32_t k = 0; k < child2.blocks_used; k++)
                        fs_free_block(fs, child2.direct[k]);
                }

                /*zero out the inode*/
                Inode blank = {0};
                fs_write_inode(fs, child_ino, &blank);
                fs_free_inode(fs, child_ino);

                /*remove the directory entry*/
                memset(&entries[si], 0, sizeof(DirEntry));
                entries[si].inode_num = DIR_ENTRY_FREE;
                fs_write_block(fs, blk, buf);
                fs_flush_superblock(fs);
                fs_flush_bitmaps(fs);
                return 0;
            }
        }
    }
    fprintf(stderr, "rm: '%s' not found\n", name);
    return -1;
}

int fs_list_dir(FS *fs, int dir_ino) 
{
    Inode dir;
    if (fs_read_inode(fs, dir_ino, &dir) != 0) return -1;
    if (dir.type != FT_DIR) { fprintf(stderr, "ls: not a directory\n"); return -1; }

    printf("%-4s  %-8s  %s\n", "INO", "TYPE", "NAME");
    printf("----  --------  --------------------\n");
    for (int bi = 0; bi < (int)dir.blocks_used; bi++) 
    {
        uint8_t buf[BLOCK_SIZE];
        if (fs_read_block(fs, dir.direct[bi], buf) != 0) return -1;
        DirEntry *entries = (DirEntry *)buf;

        for (int si = 0; si < (int)ENTRIES_PER_BLOCK; si++) 
        {
            if (entries[si].inode_num != DIR_ENTRY_FREE) 
            {
                const char *tstr = (entries[si].type == FT_DIR) ? "<DIR>" : "<FILE>";
                printf("%-4u  %-8s  %s\n",
                       entries[si].inode_num, tstr, entries[si].name);
            }
        }
    }

    return 0;
}
