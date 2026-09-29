#pragma once
#include <linux/types.h>

#define FT_FS_MAGIC                     0x46573432
#define FT_BLOCK_SIZE                   4096
#define FT_MIN_BLOCKS                   4

typedef struct super_data
{
    __u32	magic;
    __u32    	blocks_bitmap_block;
    __u32    	inodes_count;
    __u32    	blocks_count;
    __u32    	free_inodes;
    __u32    	free_blocks;
}__attribute__((packed)) ft_super_data;

typedef struct  ft_super
{
	ft_super_data data;
    __u8	inodes_bitmap[FT_BLOCK_SIZE - sizeof(ft_super_data)];
} __attribute__((packed)) ft_super;

#define FT42_FREE 0x00
#define FT42_FIFO 0x01
#define FT42_CHR  0x02
#define FT42_DIR  0x04
#define FT42_BLK  0x06
#define FT42_REG  0x08
#define FT42_LINK 0x0A
#define FT42_SOCK 0x0C

#define FT42_IFREE 0x0000
#define FT42_IFIFO 0x1000
#define FT42_ICHR  0x2000
#define FT42_IDIR  0x4000
#define FT42_IBLK  0x6000
#define FT42_IREG  0x8000
#define FT42_ILINK 0xA000
#define FT42_ISOCK 0xC000

typedef struct  ft_inode
{
    __u8     links;
    __u8     level;
    __u16    mode;
    __u32    uid;
    __u32    gid;
    __u32    block;
    __u32    size;
    __u32    ctime;
    __u32    mtime;
    __u32    atime;
} __attribute__((packed)) ft_inode;

typedef struct ft_dentry
{
    __u32    ino_idx;
    __u8     type;
    char     name[251];
} __attribute__((packed)) ft_dentry;

#define FT_INODE_BITMAP_SIZE            FT_BLOCK_SIZE - sizeof(ft_super_data)
#define FT_MAX_INODES_COUNT	            FT_INODE_BITMAP_SIZE * 8
#define FT_INODES_PER_BLOCK	            FT_BLOCK_SIZE / sizeof(ft_inode)
#define FT_MAX_INODES_BLOCKS            FT_MAX_INODES_COUNT / FT_INODES_PER_BLOCK
#define FT_BITMAP_CAPACITY_PER_BLOCK    FT_BLOCK_SIZE * 8 // block granularity
#define FT_DENTRY_PER_BLOCK             FT_BLOCK_SIZE / sizeof(ft_dentry)
