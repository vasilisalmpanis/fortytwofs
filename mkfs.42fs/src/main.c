#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include "../fortytwofs.h"

#define INODE_SHARE 5
#define min(a,b) \
    ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })
#define assert_pos(expected_pos, msg) do    \
{                                           \
    off_t pos = lseek(fs_fd, 0, SEEK_CUR);  \
    if (pos == (off_t)-1) {                 \
        perror("lseek");                    \
    }                                       \
    if ((size_t)pos != expected_pos) {      \
        fprintf(stderr, "pos: %lx, "        \
                "expected_pos: %x"          \
                "\n"                        \
                ,(long unsigned int) pos,   \
                (unsigned int)expected_pos);\
        error(msg);                         \
    }                                       \
} while (0)                                 \

struct metadata_size {
    uint32_t total_blocks;
    uint32_t inode_blocks;
    uint32_t blocks_bitmap_blocks;
};

void error(char *str) {
    fprintf(stderr, "mkfs.42fs: %s\n", str);
    exit(1);
}

void calc_metadata(struct stat *st, struct metadata_size *data) {
    size_t free_blocks = 0;
    size_t inode_blocks_share = 0;

    data->total_blocks = st->st_size / FT_BLOCK_SIZE;

    // Total blocks
    if (data->total_blocks < FT_MIN_BLOCKS)
        error("Not enough space to create a filesystem.");
    free_blocks = data->total_blocks - 1;

    // Inodes blocks
    inode_blocks_share = INODE_SHARE * free_blocks / 100;
    if (inode_blocks_share == 0)
        inode_blocks_share = 1;

    data->inode_blocks = min(FT_MAX_INODES_BLOCKS, inode_blocks_share);
    free_blocks -= data->inode_blocks;

    // Block bitmap blocks
    data->blocks_bitmap_blocks = free_blocks / FT_BITMAP_CAPACITY_PER_BLOCK;
    if (data->blocks_bitmap_blocks == 0)
        data->blocks_bitmap_blocks = 1;
}

void write_one_dentry(int fs_fd, ft_dentry *dentry) {
    uint32_t written = 0;
    ssize_t written_oneshot = 0;

    while (written < sizeof(ft_dentry)) {
        const uint8_t *ptr = (const uint8_t *)dentry;
        written_oneshot = write(fs_fd, ptr + written, sizeof(ft_dentry) - written);
        if (written_oneshot < 0) {
            if (errno == EINTR)
                continue;
            perror("mkfs.42fs");
            exit(1);
        }
        written += written_oneshot;
    }
}

void write_dentries(int fs_fd) {
    ft_dentry dentry = {0};

    // dir .
    dentry.ino_idx = 0;
    dentry.type = FT_DIR;
    dentry.name[0] = '.';
    write_one_dentry(fs_fd, &dentry);

    // dir ..
    dentry.name[0] = '.';
    dentry.name[1] = '.';
    write_one_dentry(fs_fd, &dentry);

    memset(&dentry, 0, sizeof(ft_dentry));
    for (size_t i = 0; i < FT_DENTRY_PER_BLOCK - 2; i++) {
        write_one_dentry(fs_fd, &dentry);
    }
}

void write_one_inode(int fs_fd, ft_inode *inode) {
    uint32_t written = 0;
    ssize_t written_oneshot = 0;

    while (written < sizeof(ft_inode)) {
        const uint8_t *ptr = (const uint8_t *)inode;
        written_oneshot = write(fs_fd, ptr + written, sizeof(ft_inode) - written);
        if (written_oneshot < 0) {
            if (errno == EINTR)
                continue;
            perror("mkfs.42fs");
            exit(1);
        }
        written += written_oneshot;
    }
}

void write_inodes(int fs_fd, struct metadata_size *data) {
    ft_inode inode = {0};

    if (lseek(fs_fd, FT_BLOCK_SIZE, SEEK_SET) < 0) {
        perror("mkfs.42fs: lseek");
        exit(1);
    }

    inode.type = FT_DIR;
    inode.level = 0;
    inode.block = /* superblock */ 1 + data->blocks_bitmap_blocks + data->inode_blocks;
    inode.gid = 0;
    inode.uid = 0;
    inode.mode = 0755;
    inode.ctime = (uint32_t)time(NULL);
    inode.mtime = inode.ctime;
    inode.size = FT_BLOCK_SIZE;
    write_one_inode(fs_fd, &inode);
    memset(&inode, 0, sizeof(ft_inode));

    for (size_t i = 0; i < FT_INODES_PER_BLOCK * data->inode_blocks - 1; i++) {
        write_one_inode(fs_fd, &inode);
    }
}

void write_superblock(int fs_fd, struct metadata_size *data, ft_super *superblock) {
    size_t written = 0;
    ssize_t written_oneshot = 0;
    uint32_t bitmap_val = 0x00000001;

    superblock->magic = FT_FS_MAGIC;
    superblock->free_blocks = data->total_blocks - data->inode_blocks \
                              - data->blocks_bitmap_blocks - 1 - 1;
    superblock->free_inodes = data->inode_blocks * FT_INODES_PER_BLOCK - 1;
    superblock->blocks_bitmap_block = 1 + data->inode_blocks;
    superblock->blocks_count = data->total_blocks;
    superblock->inodes_count = data->inode_blocks * FT_INODES_PER_BLOCK;

    while (written < sizeof(*superblock)) {
        const uint8_t *ptr = (const uint8_t *)superblock;
        written_oneshot = write(fs_fd, ptr + written, sizeof(*superblock) - written);
        if (written_oneshot < 0) {
            if (errno == EINTR)
                continue;
            perror("mkfs.42fs");
            exit(1);
        }
        written += written_oneshot;
    }

    while (written < FT_BLOCK_SIZE) {
        written_oneshot = write(fs_fd, (const void *)&bitmap_val, sizeof(uint32_t));
        if (written_oneshot < 0) {
            if (errno == EINTR)
                continue;
            perror("mkfs.42fs");
            exit(1);
        }
        if (bitmap_val == 0x00000001)
            bitmap_val = 0;
        written += written_oneshot;
    }

    assert_pos(FT_BLOCK_SIZE,
            "Logic bug, wrote more than superblock");
}

void write_block_bitmap(int fs_fd, struct metadata_size *data) {
    size_t total_used_blocks = 1 + data->inode_blocks + data->blocks_bitmap_blocks + 1;
    ssize_t written_oneshot = 0;
    size_t blocks_written = 0;
    size_t written = 0;

    /*
     * Block bitmap marks as taken blocks taken also by superblock, inodes blocks
     * and the bitmap itself. So a block index is absolute
     */
    while (written < data->blocks_bitmap_blocks * FT_BLOCK_SIZE) {
        int32_t to_write = total_used_blocks - blocks_written;
        uint32_t val = 0;
        if (to_write > 0) {
            if (to_write >= 32) {
                val = 0xFFFFFFFF;
                blocks_written += 32;
            } else {
                val = 0xFFFFFFFF >> (32 % to_write);
                blocks_written += to_write;
            }
        }
        written_oneshot  = write(fs_fd, (const void *) &val, sizeof(uint32_t));
        if (written_oneshot != sizeof(uint32_t)) {
            perror("mkfs.42fs");
            exit(1);
        }
        written += written_oneshot;
    }
    assert_pos((total_used_blocks - 1) * FT_BLOCK_SIZE,
            "Logic bug wrote past the block bitmap");
}

void write_metadata(int fs_fd, struct metadata_size *data) {
    ft_super superblock = {0};

    write_superblock(fs_fd, data, &superblock);

    if (lseek(fs_fd, superblock.blocks_bitmap_block * FT_BLOCK_SIZE, SEEK_SET) < 0) {
        perror("mkfs.42fs: lseek");
        exit(1);
    }

    write_block_bitmap(fs_fd, data);

    write_dentries(fs_fd);
    assert_pos((2 + data->inode_blocks + data->blocks_bitmap_blocks) * FT_BLOCK_SIZE,
            "wrote past end of first block");

    write_inodes(fs_fd, data);
    assert_pos(superblock.blocks_bitmap_block * FT_BLOCK_SIZE,
            "Logic bug wrote past the end of inodes blocks");
}

int main(int ac, char **av) {
    struct stat statbuf;
    struct metadata_size data = {0};
    int fs_fd;

    if (ac < 2) {
        fprintf(stderr, "Usage: %s [path]\n", av[0]);
        exit(1);
    }
    if (stat(av[1], &statbuf) < 0) {
        perror(av[0]);
        exit(1);
    }
    if (!(S_ISBLK(statbuf.st_mode) || S_ISREG(statbuf.st_mode))) {
        fprintf(stderr, "%s: not a regular file or block device\n", av[0]);
        exit(1);
    }
    calc_metadata(&statbuf, &data);

    fs_fd = open(av[1], O_RDWR);
    if (fs_fd < 0) {
        perror(av[0]);
        exit(1);
    }
    write_metadata(fs_fd, &data);

    close(fs_fd);
    return(0);
}
