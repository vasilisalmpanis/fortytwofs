/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FT_FS_H
#define _FT_FS_H
#define pr_fmt(fmt) "fortytwofs: " fmt

#include <linux/fs.h>
#include <linux/cleanup.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/byteorder/generic.h>
#include "fortytwofs.h"

typedef struct fortytwofs_super_info {
	struct buffer_head *bh;
	ft_super *super;
} ft_super_info;

typedef struct fortytwofs_inode_info {
	struct buffer_head *bh;
	ft_inode *inode;
} ft_inode_info;

struct inode *fortyfs_iget(struct super_block *sb, unsigned long ino);
struct inode *fortytwofs_new_inode(struct inode *dir, umode_t mode,
				   const struct qstr *qstr);
int ftfs_add_dentry(struct inode *dir, struct inode *child,
		    const struct qstr *qstr);
int ftfs_make_empty(struct inode *inode, struct inode *parent);
int ftfs_alloc_new_block(struct super_block *sb);
int ftfs_zalloc_new_block(struct super_block *sb);
int ftfs_next_level(struct inode *inode);

extern const struct file_operations fortytwofs_file_ops;
extern const struct file_operations fortytwofs_dir_ops;
extern const struct inode_operations fortytwofs_dir_inode_operations;

#endif /* _FT_FS_H */
