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

struct inode *fortyfs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *inode;
	struct buffer_head *bh;
	ft_super *super_ft = sb->s_fs_info;
	int block;
	int ino_in_block;

	if (ino >= super_ft->inodes_count)
		return ERR_PTR(-EINVAL);

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;

	struct ft_inode *inode_ft __free(kfree) = kzalloc(sizeof(*inode_ft),
							  GFP_KERNEL);
	if (!inode_ft) {
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
	memcpy(inode_ft, bh->b_data + ino_in_block * sizeof(*inode_ft),
	       sizeof(*inode_ft));
	brelse(bh);

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
	inode->i_private = no_free_ptr(inode_ft);
	unlock_new_inode(inode);
	return inode;
}
