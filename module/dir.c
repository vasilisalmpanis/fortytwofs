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
			      fs_ftype_to_dtype(dentry->type)))
			break;
	}
	brelse(bh);
	return 0;
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
