// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

static int ftfs_dir_open(struct inode *inode, struct file *file)
{
	file->private_data = kzalloc(sizeof(u64), GFP_KERNEL);
	if (!file->private_data)
		return -ENOMEM;
	return 0;
}

static int ftfs_dir_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
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
static int ftfs_free_space_for_dentry(struct inode *dir,
				      __u32 block,
				      __u8 lvl,
				      int *dentry_idx)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh = sb_bread(sb, block);
	int ret = 0;

	if (!bh)
		return -EIO;
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
				if (lvl == 1) {
					// Inclease inode size to 1 block
					i_size_write(dir, dir->i_size +
						     FT_BLOCK_SIZE);
					mark_inode_dirty(dir);
				}
			}
			ret = ftfs_free_space_for_dentry(dir,
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
	int block;
	int err;

	if (qstr->len > FT_MAX_NAME_LEN)
		return -ENAMETOOLONG;
	block = ftfs_free_space_for_dentry(dir, raw->block, raw->level,
					   &dentry_idx);
	if (block < 0)
		return block;
	if (block == 0) {
		err = ftfs_next_level(dir);
		if (err)
			return err;
		block = ftfs_free_space_for_dentry(dir, raw->block,
						   raw->level, &dentry_idx);
		if (block < 0)
			return block;
		if (block == 0)
			return -ENOSPC;
	}
	bh = sb_bread(dir->i_sb, block);
	if (!bh)
		return -EIO;
	dentry_arr = (ft_dentry *)bh->b_data;
	new_dentry = dentry_arr + dentry_idx;
	memset(new_dentry, 0, sizeof(*new_dentry));
	new_dentry->ino_idx = cpu_to_le32(child->i_ino);
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

/**
 * writes ft_dentry matched to res (if it is not NULL) and buffer head
 *    containing it to bh_res (if it is not NULL)
 * bh_else should be freed with brelse after use
 * returns 0 if found (cmp returned false on some dentry)
 * returns 1 if checked all dentries and cmp was always true
 * returns negative value if error occurred
 */
static int ftfs_check_dentries_until_false(struct super_block *sb,
					   __u32 block, __u8 lvl,
					   bool (*cmp)(ft_dentry *, void *),
					   void *cmp_arg,
					   ft_dentry **res,
					   struct buffer_head **bh_res)
{
	struct buffer_head *bh;
	int ret = 1;

	if (block == 0)
		return 1;
	bh = sb_bread(sb, block);
	if (!bh)
		return -EIO;
	if (lvl == 0) {
		ft_dentry *dentry_arr = (ft_dentry *)bh->b_data;

		for (int idx = 0; idx < FT_DENTRY_PER_BLOCK; idx++) {
			ft_dentry *de = &dentry_arr[idx];

			if (!cmp(de, cmp_arg)) {
				if (res)
					*res = de;
				ret = 0;
				if (bh_res)
					*bh_res = bh;
				else
					brelse(bh);
				goto success;
			}
		}
	} else {
		__u32 *blks = (__u32 *)bh->b_data;

		for (int i = 0; i < FT_PTRS_PER_BLOCK; i++) {
			ret = ftfs_check_dentries_until_false(sb, blks[i],
							      lvl - 1,
							      cmp, cmp_arg,
							      res, bh_res);
			if (ret != 1)
				break;
		}
	}
	brelse(bh);
success:
	return ret;
}

struct ftfs_readdir_state {
	struct dir_context *ctx;
	loff_t off;
};

static bool ftfs_emit_dir(ft_dentry *dentry, void *data)
{
	struct ftfs_readdir_state *st = data;

	st->off += sizeof(ft_dentry);
	if (st->off <= st->ctx->pos)
		return true;
	if (dentry->type != FT42_FREE) {
		if (!dir_emit(st->ctx, dentry->name, strlen(dentry->name),
			      le32_to_cpu(dentry->ino_idx), dentry->type))
			return false;
	}
	st->ctx->pos = st->off;
	return true;
}

static int ftfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	ft_inode_info *info = (ft_inode_info *)inode->i_private;
	ft_inode *raw = info->inode;
	struct ftfs_readdir_state st = {
		.ctx = ctx,
		.off = 0
	};
	int ret;

	if (ctx->pos >= inode->i_size)
		return 0;
	ret = ftfs_check_dentries_until_false(inode->i_sb, raw->block,
					      raw->level,
					      ftfs_emit_dir, &st, NULL, NULL);
	if (ret < 0)
		return ret;
	return 0;
}

static bool is_initial_dots(ft_dentry *dentry, void *unused)
{
	ssize_t len;

	(void)unused;
	if (dentry->type == FT42_FREE)
		return true;
	len = strlen(dentry->name);
	if (len > 2)
		return false;
	if (dentry->name[0] != '.')
		return false;
	if (len == 1)
		return true;
	if (dentry->name[1] == '.')
		return true;
	return false;
}

/**
 * returns 1 if dir is empty, 0 otherwise, can return error (negative value)
 */
int ftfs_empty_dir(struct inode *inode)
{
	ft_inode_info *info = inode->i_private;
	ft_inode *raw = info->inode;

	return ftfs_check_dentries_until_false(inode->i_sb, raw->block,
					       raw->level,
					       is_initial_dots, NULL,
					       NULL, NULL);
}

static bool match_name(ft_dentry *dentry, void *arg)
{
	ssize_t len;
	const struct qstr *name = arg;

	if (dentry->type == FT42_FREE)
		return true;
	len = strlen(dentry->name);
	if (name->len != len)
		return true;
	if (strcmp(dentry->name, name->name))
		return true;
	return false;
}

int ftfs_remove_dentry(struct inode *inode, const struct qstr *name)
{
	ft_inode_info *info = inode->i_private;
	ft_inode *raw = info->inode;
	struct buffer_head *bh = NULL;
	ft_dentry *res = NULL;
	int err = 0;

	err = ftfs_check_dentries_until_false(inode->i_sb, raw->block,
					      raw->level, match_name,
					      (void *)name, &res, &bh);
	if (err < 0)
		return err;
	if (err == 1)
		return -ENOENT;
	memset(res, 0, sizeof(*res));
	mark_buffer_dirty(bh);
	brelse(bh);
	return 0;
}

const struct file_operations ftfs_dir_ops = {
	.open			= ftfs_dir_open,
	.release		= ftfs_dir_release,
	// .llseek		= ftfs_dir_llseek,
	.read			= generic_read_dir,
	.iterate_shared		= ftfs_readdir,
	// .unlocked_ioctl	= ftfs_ioctl,
	// .fsync		= ftfs_fsync,
};
