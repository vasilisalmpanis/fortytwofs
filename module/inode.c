// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

static const struct inode_operations fortyfs_inode_operations = {
	// .listxattr		= fortytwofs_listxattr,
	// .getattr		= fortytwofs_getattr,
	// .setattr		= fortytwofs_setattr,
	// .get_inode_acl	= fortytwofs_get_acl,
	// .set_acl		= fortytwofs_set_acl,
	// .fiemap		= fortytwofs_fiemap,
	// .fileattr_get	= fortytwofs_fileattr_get,
	// .fileattr_set	= fortytwofs_fileattr_set,
};

void ftfs_set_inode_ops(struct inode *inode)
{
	inode->i_op = &fortyfs_inode_operations;
	if (S_ISREG(inode->i_mode)) {
		inode->i_fop = &fortytwofs_file_ops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &fortytwofs_dir_inode_operations;
		inode->i_fop = &fortytwofs_dir_ops;
	}
}

void ftfs_set_inode_data(struct inode *inode)
{
	ft_inode_info *info = (ft_inode_info *)inode->i_private;
	ft_inode *raw = info->inode;

	inode->i_mode = le16_to_cpu(raw->mode);
	i_uid_write(inode, le32_to_cpu(raw->uid));
	i_gid_write(inode, le32_to_cpu(raw->gid));
	set_nlink(inode, le16_to_cpu((__u16)raw->links));
	inode->i_size = le32_to_cpu(raw->size);
	inode_set_atime(inode, (signed int)le32_to_cpu(raw->atime), 0);
	inode_set_ctime(inode, (signed int)le32_to_cpu(raw->ctime), 0);
	inode_set_mtime(inode, (signed int)le32_to_cpu(raw->mtime), 0);
	ftfs_set_inode_ops(inode);
}

/* Take the first free inode number from the bitmap in the superblock */
static int ftfs_alloc_ino(struct super_block *sb, unsigned long *ino)
{
	ft_super_info *super_info = sb->s_fs_info;
	ft_super *super = super_info->super;
	u32 count = min_t(u32, super->data.inodes_count, FT_MAX_INODES_COUNT);
	unsigned long bit;

	if (super->data.free_inodes == 0)
		return -ENOSPC;
	bit = find_next_zero_bit_le(super->inodes_bitmap, count, 0);
	if (bit >= count)
		return -ENOSPC;

	__set_bit_le(bit, super->inodes_bitmap);
	super->data.free_inodes -= 1;
	mark_buffer_dirty(super_info->bh);
	*ino = bit;
	return 0;
}

static void ftfs_free_ino(struct super_block *sb, unsigned long ino)
{
	ft_super_info *super_info = sb->s_fs_info;
	ft_super *super = super_info->super;

	__clear_bit_le(ino, super->inodes_bitmap);
	super->data.free_inodes += 1;
	mark_buffer_dirty(super_info->bh);
}

static void ftfs_write_raw_inode(ft_inode *raw, struct inode *inode)
{
	raw->mode = cpu_to_le16(inode->i_mode);
	raw->links = inode->i_nlink;
	raw->uid = cpu_to_le32(i_uid_read(inode));
	raw->gid = cpu_to_le32(i_gid_read(inode));
	raw->size = cpu_to_le32(inode->i_size);
	raw->ctime = cpu_to_le32(inode_get_ctime_sec(inode));
	raw->mtime = cpu_to_le32(inode_get_mtime_sec(inode));
	raw->atime = cpu_to_le32(inode_get_atime_sec(inode));
}

struct inode *fortytwofs_new_inode(struct inode *dir, umode_t mode,
				   const struct qstr *qstr)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct inode *inode;
	unsigned long ino;
	ft_inode *raw;
	int error;

	ft_inode_info *info __free(kfree) = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	error = ftfs_alloc_ino(sb, &ino);
	if (error)
		return ERR_PTR(error);

	bh = sb_bread(sb, ino / FT_INODES_PER_BLOCK + 1); /* First block is SB */
	if (!bh) {
		error = -ENOMEM;
		goto free_ino;
	}

	inode = new_inode(sb);
	if (!inode) {
		error = -ENOMEM;
		goto free_bh;
	}
	inode->i_ino = ino;
	inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
	simple_inode_init_ts(inode);
	ftfs_set_inode_ops(inode);

	if (insert_inode_locked(inode) < 0) {
		error = -EIO;
		goto put_inode;
	}

	error = ftfs_add_dentry(dir, inode, qstr);
	if (error)
		goto discard_inode;

	raw = (ft_inode *)bh->b_data + ino % FT_INODES_PER_BLOCK;
	memset(raw, 0, sizeof(*raw));
	ftfs_write_raw_inode(raw, inode);
	mark_buffer_dirty(bh);

	info->bh = bh;
	info->inode = raw;
	inode->i_private = no_free_ptr(info);
	return inode;

discard_inode:
	clear_nlink(inode);
	discard_new_inode(inode);
	goto free_bh;
put_inode:
	iput(inode);
free_bh:
	brelse(bh);
free_ino:
	ftfs_free_ino(sb, ino);
	return ERR_PTR(error);
}

struct inode *fortyfs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
	struct buffer_head *bh;
	ft_super_info *super_info = sb->s_fs_info;
	ft_inode *raw;
	int block;
	int ino_in_block;

	if (ino >= super_info->super->data.inodes_count)
		return ERR_PTR(-EINVAL);

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	ft_inode_info *inode_info __free(kfree) = kzalloc(sizeof(ft_inode_info),
						   GFP_KERNEL);
	if (!inode_info) {
		iget_failed(inode);
		return ERR_PTR(-ENOMEM);
	}

	block = ino / FT_INODES_PER_BLOCK + 1;
	bh = sb_bread(sb, block);
	if (!bh) {
		pr_err("unable to read inode %lu\n", ino);
		iget_failed(inode);
		return ERR_PTR(-EINVAL);
	}
	ino_in_block = ino % FT_INODES_PER_BLOCK;
	inode_info->bh = bh;
	raw = (ft_inode *)(bh->b_data + ino_in_block * sizeof(ft_inode));
	inode_info->inode = raw;

	inode->i_private = no_free_ptr(inode_info);
	ftfs_set_inode_data(inode);

	if (inode->i_nlink == 0 && inode->i_mode == 0) {
		iget_failed(inode);
		return ERR_PTR(-ESTALE);
	}
	unlock_new_inode(inode);
	return inode;
}

int ftfs_next_level(struct inode *inode)
{
	ft_inode_info *info = (ft_inode_info *)inode->i_private;
	ft_inode *ft_inode = info->inode;
	int block = 0;
	struct buffer_head *bh = NULL;
	__u32 *blocks = NULL;

	block = ftfs_zalloc_new_block(inode->i_sb);
	if (block < 0)
		return block;

	bh = sb_bread(inode->i_sb, block);
	if (!bh) {
		// TODO:
		// free block
		return -ENOMEM;
	}
	blocks = (__u32 *)bh->b_data;
	blocks[0] = ft_inode->block;
	mark_buffer_dirty(bh);
	brelse(bh);

	ft_inode->level += 1;
	ft_inode->block = block;
	mark_buffer_dirty(info->bh);
	return 0;
}
