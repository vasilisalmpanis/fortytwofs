#pragma once
#include <stdint.h>

typedef struct  super
{
    uint32_t    magic;
    uint32_t    blocks_bitmap_block;
    uint32_t    inodes_count;
    uint32_t    blocks_count;
    uint32_t    free_inodes;
    uint32_t    free_blocks;
} __attribute__((packed)) ft_super;

#define FT_REG  0x00
#define FT_LINK 0x01
#define FT_CHR  0x02
#define FT_BLK  0x03
#define FT_DIR  0x04
#define FT_FIFO 0x05
#define FT_SOCK 0x06

typedef struct  inode
{
    uint16_t    _reverved;
    uint8_t     type;
    uint8_t     level;
    uint32_t    uid;
    uint32_t    gid;
    uint32_t    block;
    uint32_t    mode;
    uint32_t    size;
    uint32_t    ctime;
    uint32_t    mtime;
} __attribute__((packed)) ft_inode;

typedef struct dentry
{
    uint32_t    ino_idx;
    uint8_t     type;
    char        name[251];
} __attribute__((packed)) ft_dentry;

#define FT_FS_MAGIC			0x46573432
#define FT_BLOCK_SIZE			4096
#define FT_MIN_BLOCKS			4
#define FT_INODE_BITMAP_SIZE		FT_BLOCK_SIZE - sizeof(ft_super)
#define FT_MAX_INODES_COUNT		FT_INODE_BITMAP_SIZE * 8
#define FT_INODES_PER_BLOCK		FT_BLOCK_SIZE / sizeof(ft_inode)
#define FT_MAX_INODES_BLOCKS		FT_MAX_INODES_COUNT / FT_INODES_PER_BLOCK
#define FT_BITMAP_CAPACITY_PER_BLOCK	FT_BLOCK_SIZE * 8 // block granularity
#define FT_DENTRY_PER_BLOCK		FT_BLOCK_SIZE / sizeof(ft_dentry)
