// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

static struct dentry *fortytwofs_lookup(struct inode *dir,
					struct dentry *dentry,
					unsigned int flags)
{
	return ERR_PTR(-ENOENT);
}

const struct inode_operations fortytwofs_dir_inode_operations = {
	// .create		= fortytwofs_create,
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
