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
#include <linux/byteorder/generic.h>
#include "fortytwofs.h"

struct inode *fortyfs_iget(struct super_block *sb, unsigned long ino);
extern const struct file_operations fortytwofs_file_ops;
extern const struct file_operations fortytwofs_dir_ops;
extern const struct inode_operations fortytwofs_dir_inode_operations;

#endif /* _FT_FS_H */
