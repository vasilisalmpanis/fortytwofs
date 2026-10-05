// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

static int fortytwofs_dir_open(struct inode *inode, struct file *file)
{
	file->private_data = kzalloc(sizeof(u64), GFP_KERNEL);
	if (!file->private_data)
		return -ENOMEM;
	return 0;
}

static int fortytwofs_dir_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static int fortytwofs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	ft_inode_info *info = (ft_inode_info *)inode->i_private;
	ft_inode *inode_ft = info->inode;

	struct buffer_head *bh = NULL;
	ft_dentry *dentry = NULL;
	loff_t pos = ctx->pos;

	if (pos >= inode->i_size)
		return 0;
	bh = sb_bread(inode->i_sb, inode_ft->block);
	if (!bh)
		return -ENOMEM;

	while (ctx->pos < bh->b_size) {
		dentry = (ft_dentry *)(bh->b_data + ctx->pos);
		ctx->pos += sizeof(ft_dentry);
		if (dentry->type == FT42_FREE)
			continue;
		if (!dir_emit(ctx, dentry->name, strlen(dentry->name),
			      le32_to_cpu(dentry->ino_idx),
			      dentry->type))
			break;
	}
	brelse(bh);
	return 0;
}

/*
 * Returns 0 and sets ino if found, -ENOENT if not.
 */
static int ftfs_find_dentry(struct super_block *sb, __u32 block, __u8 lvl,
			    const struct qstr *name, unsigned long *ino)
{
	struct buffer_head *bh;
	int ret = -ENOENT;

	if (block == 0)
		return -ENOENT;
	bh = sb_bread(sb, block);
	if (!bh)
		return -EIO;
	if (lvl == 0) {
		ft_dentry *dentry_arr = (ft_dentry *)bh->b_data;

		for (int idx = 0; idx < FT_DENTRY_PER_BLOCK; idx++) {
			ft_dentry *de = &dentry_arr[idx];

			if (de->type == FT42_FREE)
				continue;
			if (strlen(de->name) == name->len &&
			    !memcmp(de->name, name->name, name->len)) {
				*ino = le32_to_cpu(de->ino_idx);
				ret = 0;
				break;
			}
		}
	} else {
		__u32 *blks = (__u32 *)bh->b_data;

		for (int i = 0; i < FT_PTRS_PER_BLOCK; i++) {
			ret = ftfs_find_dentry(sb, blks[i], lvl - 1, name, ino);
			if (ret != -ENOENT)
				break;
		}
	}
	brelse(bh);
	return ret;
}

int ftfs_lookup_ino(struct inode *dir, const struct qstr *name,
		    unsigned long *ino)
{
	ft_inode_info *info = (ft_inode_info *)dir->i_private;

	return ftfs_find_dentry(dir->i_sb, le32_to_cpu(info->inode->block),
				info->inode->level, name, ino);
}

/*
 * returns 0 if nothing found
 */
static int ftfs_free_space_for_dentry(struct super_block *sb,
			       __u32 block,
			       __u8 lvl,
			       int *dentry_idx)
{
	struct buffer_head *bh = sb_bread(sb, block);
	int ret = 0;

	if (!bh)
		return -ENOMEM;
	if (lvl == 0) {
		ft_dentry *dentry_arr = (ft_dentry *)bh->b_data;

		for (int idx = 0; idx < FT_DENTRY_PER_BLOCK; idx++) {
			if (dentry_arr[idx].type == FT42_FREE) {
				*dentry_idx = idx;
				ret = block;
				break;
			}
		}
	} else {
		__u32 *blocks = (__u32 *)bh->b_data;

		for (int idx = 0; idx < FT_PTRS_PER_BLOCK; idx++) {
			if (blocks[idx] == 0) {
				int new_block = ftfs_zalloc_new_block(sb);

				if (new_block < 0) {
					ret = new_block;
					break;
				}
				blocks[idx] = new_block;
				mark_buffer_dirty(bh);
			}
			ret = ftfs_free_space_for_dentry(sb,
							 blocks[idx],
							 lvl - 1,
							 dentry_idx);
			if (ret == 0)
				continue;
			break;
		}
	}
	brelse(bh);
	return ret;
}

int ftfs_add_dentry(struct inode *dir, struct inode *child,
		    const struct qstr *qstr)
{
	ft_inode_info *info = (ft_inode_info *)dir->i_private;
	ft_inode *raw = info->inode;
	struct buffer_head *bh = NULL;
	ft_dentry *new_dentry;
	int dentry_idx = 0;
	ft_dentry *dentry_arr;
	__u32 block = ftfs_free_space_for_dentry(dir->i_sb, raw->block,
						 raw->level, &dentry_idx);
	if (block < 0)
		return -ENOSPC;
	if (block == 0) {
		if (ftfs_next_level(dir) < 0)
			return -ENOSPC;
		block = ftfs_free_space_for_dentry(dir->i_sb, raw->block,
						   raw->level, &dentry_idx);
		if (block <= 0)
			return -ENOSPC;
	}
	bh = sb_bread(dir->i_sb, block);
	if (!bh)
		return -ENOMEM;
	dentry_arr = (ft_dentry *)bh->b_data;
	new_dentry = dentry_arr + dentry_idx;
	new_dentry->ino_idx = child->i_ino;
	new_dentry->type = fs_umode_to_dtype(child->i_mode);
	memcpy(new_dentry->name, qstr->name, qstr->len);
	mark_buffer_dirty(bh);
	brelse(bh);
	return 0;
}

int ftfs_make_empty(struct inode *inode, struct inode *parent)
{
	ft_inode_info *info = inode->i_private;
	struct qstr dot    = QSTR_INIT(".", 1);
	struct qstr dotdot = QSTR_INIT("..", 2);
	int block, err;

	block = ftfs_zalloc_new_block(inode->i_sb);
	if (block < 0)
		return block;
	info->inode->block = cpu_to_le32(block);
	info->inode->level = 0;
	info->inode->size = FT_BLOCK_SIZE;
	mark_buffer_dirty(info->bh);
	inode->i_size = FT_BLOCK_SIZE;

	err = ftfs_add_dentry(inode, inode, &dot);
	if (!err)
		err = ftfs_add_dentry(inode, parent, &dotdot);
	return err;
}

const struct file_operations fortytwofs_dir_ops = {
	.open			= fortytwofs_dir_open,
	.release		= fortytwofs_dir_release,
	// .llseek		= fortytwofs_dir_llseek,
	.read			= generic_read_dir,
	.iterate_shared		= fortytwofs_readdir,
	// .unlocked_ioctl	= fortytwofs_ioctl,
	// .fsync		= fortytwofs_fsync,
};
