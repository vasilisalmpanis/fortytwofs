/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FT_FS_H
#define _FT_FS_H

struct inode *fortyfs_iget(struct super_block *sb, unsigned long ino);
static const struct file_operations fortytwofs_file_ops;
extern const struct file_operations fortytwofs_dir_ops;
extern const struct inode_operations fortytwofs_dir_inode_operations;

#endif /* _FT_FS_H */
