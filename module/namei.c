// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

static int fortytwofs_create(struct mnt_idmap *idmap,
			     struct inode *dir, struct dentry *dentry,
			     umode_t mode, bool excl)
{
	(void)idmap;
	(void)excl;
	struct inode *inode = fortytwofs_new_inode(dir, mode, &dentry->d_name);

	if (IS_ERR(inode))
		return PTR_ERR(inode);
	d_instantiate(dentry, inode);
	return 0;
}

static struct dentry *fortytwofs_lookup(struct inode *dir,
					struct dentry *dentry,
					unsigned int flags)
{
	// return ERR_PTR(-ENOENT);
	// TODO: implement lookup for existing inodes
	return d_splice_alias(NULL, dentry);
}

const struct inode_operations fortytwofs_dir_inode_operations = {
	.create			= fortytwofs_create,
	.lookup			= fortytwofs_lookup,
	// .link		= fortytwofs_link,
	// .unlink		= fortytwofs_unlink,
	// .symlink		= fortytwofs_symlink,
	// .mkdir		= fortytwofs_mkdir,
	// .rmdir		= fortytwofs_rmdir,
	// .mknod		= fortytwofs_mknod,
	// .rename		= fortytwofs_rename,
	// .listxattr		= fortytwofs_listxattr,
	// .getattr		= fortytwofs_getattr,
	// .setattr		= fortytwofs_setattr,
	// .get_inode_acl	= fortytwofs_get_acl,
	// .set_acl		= fortytwofs_set_acl,
	// .tmpfile		= fortytwofs_tmpfile,
	// .fileattr_get	= fortytwofs_fileattr_get,
	// .fileattr_set	= fortytwofs_fileattr_set,
};
