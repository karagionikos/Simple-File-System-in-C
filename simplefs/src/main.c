#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*Path of the virtual disk image. The filesystem is stored inside this file.*/
#define DISK_PATH "disk.img"

/*Maximum size of the current working directory path*/
#define CWD_MAX 512

/*State*/
/*
 * fs:
 *   Holds the mounted filesystem information
 *
 * cwd_ino:
 *   inode number of the current directory
 *
 * cwd_path:
 *   Human-readable path shown in the shell prompt
 */
static FS fs;
static int cwd_ino = ROOT_INODE;
static char cwd_path[CWD_MAX] = "/";

/*Command implementations*/
/*Prints all available shell commands. This is displayed when the user types "help".*/
static void cmd_help(void) {
    printf(
        "\n"
        "File System Shell Commands\n"
        "\n"
        "  mkfs              Format disk image & create fresh File System\n"
        "  ls                List contents of current directory\n"
        "  tree              Show directory tree\n"
        "  mkdir <name>      Create subdirectory in current dir\n"
        "  cd <path>         Change current directory  (/ or relative)\n"
        "  touch <name>      Create empty file\n"
        "  write <name>      Write data to a file  (reads lines until EOF)\n"
        "  read  <name>      Print file contents\n"
        "  rm    <name>      Remove file or empty directory\n"
        "  stat  <name>      Show inode info for file/dir\n"
        "  pwd               Print current directory path\n"
        "  df                Show disk usage\n"
        "  neofetch          Show filesystem information\n"
        "  help              Show this message\n"
        "  exit / quit       Unmount & exit\n"
        "\n"
    );
}

/*
 * Creates a brand-new filesystem.
 *
 * Steps:
 * 1. Unmount old filesystem if one exists
 * 2. Format a new disk image
 * 3. Mount the new filesystem
 * 4. Reset current directory to root
 */
static void cmd_mkfs(void) 
{
    printf("Formatting %s", DISK_PATH);
    fflush(stdout);

    if (fs.disk) fs_unmount(&fs);
    if (fs_format(DISK_PATH) != 0) 
    {
        printf("FAILED\n"); return;
    }
    if (fs_mount(&fs, DISK_PATH) != 0) 
    {
        printf("format OK but mount FAILED\n"); return;
    }

    cwd_ino = ROOT_INODE;
    strcpy(cwd_path, "/");
    printf("done  (%d blocks × %d bytes = %d KB)\n",
           DISK_BLOCKS, BLOCK_SIZE, DISK_BLOCKS * BLOCK_SIZE / 1024);
}

/*
 * Displays filesystem information
 * using the already loaded superblock.
 */
static void cmd_neofetch(void)
{
    uint32_t used_blocks = (DISK_BLOCKS - DATA_START_BLK) - fs.sb.free_blocks;
    uint32_t used_inodes = INODE_COUNT - fs.sb.free_inodes;

    printf(
        "\n"
        "   SIMPLEFS   \n"
        "\n"
        "Filesystem : SimpleFS\n"
        "Blocks     : %u / %u\n"
        "Block size : %u bytes\n"
        "Inodes     : %u / %u used\n"
        "Disk image : disk.img\n\n",
        used_blocks,
        DISK_BLOCKS - DATA_START_BLK,
        fs.sb.block_size,
        used_inodes,
        INODE_COUNT
    );
}

static void cmd_ls(void) 
{
    fs_list_dir(&fs, cwd_ino);
}

static void cmd_mkdir(const char *name) 
{
    if (!name || name[0] == '\0') 
    { 
        printf("usage: mkdir <name>\n"); return; 
    }

    int ino = fs_mkdir(&fs, cwd_ino, name);
    if (ino >= 0) printf("Directory '%s' created  (inode %d)\n", name, ino);
}

static void print_tree(int ino, int depth)
{
    Inode dir;

    if (fs_read_inode(&fs, ino, &dir) != 0)
        return;

    if (dir.type != FT_DIR)
        return;


    for (int bi = 0; bi < (int)dir.blocks_used; bi++)
    {
        uint8_t buf[BLOCK_SIZE];
        if (fs_read_block(&fs, dir.direct[bi], buf) != 0)
            continue;

        DirEntry *entries = (DirEntry *)buf;
        for (int si = 0; si < (int)(BLOCK_SIZE / sizeof(DirEntry)); si++)
        {
            if (entries[si].inode_num == DIR_ENTRY_FREE)
                continue;

            if (strcmp(entries[si].name, ".") == 0 ||
                strcmp(entries[si].name, "..") == 0)
                continue;

            for (int i = 0; i < depth; i++)
                printf("|   ");

            printf("|-- %s", entries[si].name);

            if (entries[si].type == FT_DIR)
            {
                printf("/\n");
                print_tree(entries[si].inode_num, depth + 1);
            }
            else
            {
                printf("\n");
            }
        }
    }
}

static void cmd_tree(void)
{
    printf("/\n");
    print_tree(ROOT_INODE, 0);
}

static void cmd_cd(const char *arg) 
{
    if (!arg || arg[0] == '\0') 
    {
        cwd_ino = ROOT_INODE;
        strcpy(cwd_path, "/");
        return;
    }
    int target;
    if (strcmp(arg, "/") == 0) 
    {
        target = ROOT_INODE;
    } 
    else 
    {
        target = fs_resolve_path(&fs, arg, cwd_ino);
    }
    if (target < 0) 
    { 
        printf("cd: '%s': no such directory\n", arg); return; 
    }
    Inode n; fs_read_inode(&fs, target, &n);
    if (n.type != FT_DIR) 
    { 
        printf("cd: '%s': not a directory\n", arg); return; 
    }

    cwd_ino = target;

    /* Update cwd_path string */
    if (arg[0] == '/') 
    {
        strncpy(cwd_path, arg, CWD_MAX - 1);
    } 
    else if (strcmp(arg, "..") == 0) 
    {
        char *slash = strrchr(cwd_path, '/');
        if (slash && slash != cwd_path) *slash = '\0';
        else strcpy(cwd_path, "/");
    } 
    else if (strcmp(arg, ".") != 0) 
    {
        /* append component */
        if (strcmp(cwd_path, "/") != 0)
            strncat(cwd_path, "/", CWD_MAX - strlen(cwd_path) - 1);
        strncat(cwd_path, arg, CWD_MAX - strlen(cwd_path) - 1);
    }
}

static void cmd_touch(const char *name) 
{
    if (!name || name[0] == '\0') 
    { 
        printf("usage: touch <name>\n"); return; 
    }

    int ino = fs_create(&fs, cwd_ino, name);
    if (ino >= 0) printf("File '%s' created  (inode %d)\n", name, ino);
}

static void cmd_write(const char *name) 
{
    if (!name || name[0] == '\0') { printf("usage: write <name>\n"); return; }

    int ino = fs_lookup(&fs, cwd_ino, name);
    if (ino < 0) 
    {
        printf("write: '%s' not found - creating it\n", name);
        ino = fs_create(&fs, cwd_ino, name);
        if (ino < 0) return;
    }

    Inode f; fs_read_inode(&fs, ino, &f);
    if (f.type != FT_FILE) 
    { 
        printf("write: '%s' is a directory\n", name); return; 
    }

    printf("Enter text (Ctrl-D to finish):\n");

    uint8_t *buf = NULL;
    size_t cap = 0, used = 0;
    char line[1024];

    while (fgets(line, sizeof(line), stdin)) 
    {
        size_t ll = strlen(line);
        if (used + ll >= cap) 
        {
            cap = cap ? cap * 2 : 4096;
            buf = realloc(buf, cap);
            if (!buf) { printf("write: out of memory\n"); return; }
        }
        memcpy(buf + used, line, ll);
        used += ll;
    }
    clearerr(stdin);  /* reset EOF so the shell can continue */

    int written = fs_write_file(&fs, ino, buf, (uint32_t)used);
    free(buf);
    if (written >= 0)
        printf("Wrote %d bytes to '%s'\n", written, name);
}

static void cmd_read(const char *name) 
{
    if (!name || name[0] == '\0') 
    { 
        printf("usage: read <name>\n"); return; 
    }

    int ino = fs_lookup(&fs, cwd_ino, name);
    if (ino < 0) 
    { 
        printf("read: '%s' not found\n", name); return; 
    }

    Inode f; fs_read_inode(&fs, ino, &f);
    if (f.type != FT_FILE) 
    { 
        printf("read: '%s' is a directory\n", name); return; 
    }
    if (f.size == 0)        
    {
         printf("(empty file)\n"); return; 
    }

    uint8_t *buf = malloc(f.size + 1);
    if (!buf) 
    { 
        printf("read: out of memory\n"); return; 
    }

    int n = fs_read_file(&fs, ino, buf, f.size);
    if (n < 0) 
    { 
        printf("read: I/O error\n"); free(buf); return; 
    }
    buf[n] = '\0';

    printf("%s (%d bytes)\n", name, n);
    fwrite(buf, 1, n, stdout);

    if (buf[n-1] != '\n') putchar('\n');
    printf("\n");
    free(buf);
}

static void cmd_rm(const char *name) 
{
    if (!name || name[0] == '\0') 
    { 
        printf("usage: rm <name>\n"); return; 
    }
    if (fs_unlink(&fs, cwd_ino, name) == 0)
        printf("'%s' removed\n", name);
}

static void cmd_stat(const char *name) 
{
    if (!name || name[0] == '\0') 
    { 
        printf("usage: stat <name>\n"); return; 
    }

    int ino = fs_lookup(&fs, cwd_ino, name);
    if (ino < 0) { printf("stat: '%s' not found\n", name); return; }

    Inode f; fs_read_inode(&fs, ino, &f);
    printf("\ninode: %d\n", ino);
    printf("  type: %s\n", f.type == FT_DIR ? "directory" : "file");
    printf("  size: %u bytes\n", f.size);
    printf("  blocks used: %u\n", f.blocks_used);
    printf("  direct ptrs:");

    for (uint32_t i = 0; i < f.blocks_used; i++) printf(" %u", f.direct[i]);
    printf("\n\n");
}

static void cmd_pwd(void) 
{
    printf("%s\n", cwd_path);
}

static void cmd_df(void) 
{
    uint32_t used_blk  = (DISK_BLOCKS - DATA_START_BLK) - fs.sb.free_blocks;
    uint32_t used_ino  = INODE_COUNT - fs.sb.free_inodes;
    printf("\n  Disk: %s\n", DISK_PATH);
    printf("  Block size : %u bytes\n", fs.sb.block_size);
    printf("  Total data : %u blocks  (%u KB)\n",
           DISK_BLOCKS - DATA_START_BLK,
           (DISK_BLOCKS - DATA_START_BLK) * BLOCK_SIZE / 1024);
    printf("  Used       : %u blocks  (%u KB)\n",
           used_blk, used_blk * BLOCK_SIZE / 1024);
    printf("  Free       : %u blocks  (%u KB)\n",
           fs.sb.free_blocks, fs.sb.free_blocks * BLOCK_SIZE / 1024);
    printf("  Inodes     : %u/%u used\n\n", used_ino, INODE_COUNT);
}

/*
 * Main interactive shell.
 *
 * Reads commands from the user,
 * separates command and argument,
 * then calls the matching function.
 */
static void run_shell(void) 
{
    char line[1024];

    printf("\n  Simple File System - virtual disk-based file system\n");
    printf("  Type 'help' for a list of commands.\n\n");

    while (1) 
    {
        printf("simplefs:%s> ", cwd_path);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* strip newline */
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') continue;

        /* split into cmd + arg */
        char *cmd = strtok(line, " \t");
        char *arg = strtok(NULL, "\n");  /* rest of line */
        if (arg) while (*arg == ' ' || *arg == '\t') arg++; /* ltrim */

        if (!cmd) continue;

        /*
         * Command dispatcher.
         * Calls the correct handler.
         */
        if (strcmp(cmd, "mkfs") == 0) { cmd_mkfs(); }
        else if (strcmp(cmd, "ls") == 0) { cmd_ls(); }
        else if (strcmp(cmd, "tree") == 0) {cmd_tree();}
        else if (strcmp(cmd, "mkdir") == 0) { cmd_mkdir(arg); }
        else if (strcmp(cmd, "cd") == 0) { cmd_cd(arg); }
        else if (strcmp(cmd, "touch") == 0) { cmd_touch(arg); }
        else if (strcmp(cmd, "write") == 0) { cmd_write(arg); }
        else if (strcmp(cmd, "read") == 0) { cmd_read(arg); }
        else if (strcmp(cmd, "rm") == 0) { cmd_rm(arg); }
        else if (strcmp(cmd, "stat") == 0) { cmd_stat(arg); }
        else if (strcmp(cmd, "pwd") == 0) { cmd_pwd(); }
        else if (strcmp(cmd, "df") == 0) { cmd_df(); }
        else if (strcmp(cmd, "neofetch") == 0) {cmd_neofetch();}
        else if (strcmp(cmd, "help") == 0) { cmd_help(); }
        else if (strcmp(cmd, "exit") == 0 ||
                 strcmp(cmd, "quit") == 0)  { break; }
        else { printf("Unknown command '%s'  (try 'help')\n", cmd); }
    }
}

/*
 * Program entry point.
 *
 * Startup sequence:
 *
 * 1. Try to mount existing filesystem
 * 2. If missing, create a new one
 * 3. Start shell
 * 4. Save and unmount filesystem on exit
 */
int main(void) 
{
    /*
     * Change terminal window title.
     * Supported by most Linux terminals.
     */
    #ifdef __linux__
    printf("\033]0;SimpleFS Terminal\007");
    #endif

    /* Try to mount an existing disk; if that fails, auto-format a fresh one */
    if (fs_mount(&fs, DISK_PATH) != 0) 
    {
        printf("[boot] No disk image found - formatting new disk at '%s'\n", DISK_PATH);
        if (fs_format(DISK_PATH) != 0) 
        {
            fprintf(stderr, "Fatal: could not create disk image\n");
            return 1;
        }
        if (fs_mount(&fs, DISK_PATH) != 0) 
        {
            fprintf(stderr, "Fatal: could not mount freshly formatted disk\n");
            return 1;
        }
        printf("[boot] Disk formatted and mounted successfully.\n");
    } 
    else 
    {
        printf("[boot] Mounted existing disk '%s'\n", DISK_PATH);
    }

    run_shell();

    fs_unmount(&fs);
    printf("Goodbye.\n");

    return 0;
}
