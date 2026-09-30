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

struct inode *fortytwofs_new_inode(struct inode *dir, umode_t mode,
				   const struct qstr *qstr)
{
	struct inode *inode = NULL;
	struct buffer_head *bh;
	struct super_block *sb;
	ft_inode_info *info = NULL;
	ft_super_info *super_info = NULL;
	ft_super *super = NULL;
	ft_inode *raw_inode = NULL;
	__u32 inode_index = 0;
	__u32 inode_block = 0;
	__u32 error = 0;
	__u8 old_byte = 0;
	int i = 0;

	sb = dir->i_sb;
	super_info = sb->s_fs_info;
	super = super_info->super;

	if (super->data.free_inodes == 0) {
		error = -ENOSPC;
		goto err;
	}

	inode = new_inode(sb);
	if (!inode) {
		error = -ENOMEM;
		goto err;
	}

	// Go to bitmap and get index of free inode and mark it as taken
	for (i = 0; i < FT_INODE_BITMAP_SIZE; i++) {
		__u8 byte = le32_to_cpu(super->inodes_bitmap[i]);

		if (byte == 0xFF)
			continue;
		if (inode_index != 0)
			break;

		for (int j = 0; j < sizeof(__u8); j++) {
			__u8 mask = 1 << j;

			if ((mask & byte) == 0) {
				// Found the index
				byte |= mask;
				old_byte = le32_to_cpu(super->inodes_bitmap[i]);
				super->inodes_bitmap[i] = cpu_to_le32(byte);
				inode_index = i * sizeof(__u8) + j;
				break;
			}
		}
	}
	if (inode_index == 0) {
		error = -ENOSPC;
		goto free_inode;
	}
	super->data.free_inodes -= 1;

	// go to inodes array and write this inode there
	// - calculate which inode block
	// - read this block
	// - go to offset and write new inode
	// - mark_buffer_dirty to flush

	inode_block = inode_index / FT_INODES_PER_BLOCK + 1; /* First block is SB */
	bh = sb_bread(sb, inode_block);
	if (!bh) {
		error = -ENOMEM;
		goto rollback_sb;
	}
	info = kmalloc(sizeof(ft_super_info), GFP_KERNEL);
	if (!info) {
		brelse(bh);
		error = -ENOMEM;
		goto rollback_sb;
	}
	inode->i_private = info;
	simple_inode_init_ts(inode);
	raw_inode = (ft_inode *)bh->b_data;
	raw_inode += (inode_index % FT_INODES_PER_BLOCK);

	info->bh = bh;
	info->inode = raw_inode;

	raw_inode->block = 0;
	raw_inode->level = 0;
	raw_inode->uid = 0;
	raw_inode->gid = 0;
	raw_inode->mode = cpu_to_le32(mode);
	raw_inode->links = cpu_to_le32(1);
	raw_inode->size = 0;
	raw_inode->ctime = cpu_to_le32(inode_get_ctime_sec(inode));
	raw_inode->mtime = cpu_to_le32(inode_get_mtime_sec(inode));
	raw_inode->atime = cpu_to_le32(inode_get_atime_sec(inode));

	inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
	inode->i_ino = inode_index;
	ftfs_set_inode_data(inode);

	if (insert_inode_locked(inode) < 0) {
		brelse(bh);
		error = -EIO;
		goto rollback_sb;
	}
	/* Flush Inode to disk */
	mark_buffer_dirty(bh);

	/* Flush SB to disk */
	mark_buffer_dirty(super_info->bh);
	ftfs_add_dentry(dir, inode, qstr);
	return inode;
rollback_sb:
	super->data.free_inodes += 1;
	super->inodes_bitmap[i] = cpu_to_le32(old_byte);
	mark_buffer_dirty(super_info->bh);
free_inode:
	iput(inode);
err:
	return ERR_PTR(error);
}

struct inode *fortyfs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
	struct buffer_head *bh;
	ft_super *super_ft = sb->s_fs_info;
	ft_inode *raw;
	int block;
	int ino_in_block;

	if (ino >= super_ft->data.inodes_count)
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
