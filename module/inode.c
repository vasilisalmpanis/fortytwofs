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

struct inode *fortytwofs_new_inode(struct inode *dir, umode_t mode, char *name)
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
	/* Flush Inode to disk */
	mark_buffer_dirty(bh);

	/* Flush SB to disk */
	mark_buffer_dirty(super_info->bh);

	return inode;
	// go to dir and write ft_dentry for new inode into it.
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
	ft_inode *inode_ft;
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
	inode_ft = (ft_inode *)(bh->b_data + ino_in_block * sizeof(ft_inode));
	inode_info->inode = inode_ft;

	inode->i_mode = le16_to_cpu(inode_ft->mode);
	i_uid_write(inode, le32_to_cpu(inode_ft->uid));
	i_gid_write(inode, le32_to_cpu(inode_ft->gid));
	set_nlink(inode, le16_to_cpu((__u16)inode_ft->links));
	inode->i_size = le32_to_cpu(inode_ft->size);
	inode_set_atime(inode, (signed int)le32_to_cpu(inode_ft->atime), 0);
	inode_set_ctime(inode, (signed int)le32_to_cpu(inode_ft->ctime), 0);
	inode_set_mtime(inode, (signed int)le32_to_cpu(inode_ft->mtime), 0);
	if (inode->i_nlink == 0 && inode->i_mode == 0) {
		iget_failed(inode);
		return ERR_PTR(-ESTALE);
	}

	inode->i_op = &fortyfs_inode_operations;
	if (S_ISREG(inode->i_mode)) {
		inode->i_fop = &fortytwofs_file_ops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &fortytwofs_dir_inode_operations;
		inode->i_fop = &fortytwofs_dir_ops;
	}
	inode->i_private = no_free_ptr(inode_info);
	unlock_new_inode(inode);
	return inode;
}
