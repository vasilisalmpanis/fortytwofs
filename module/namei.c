// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

static int fortytwofs_mkdir(struct mnt_idmap *idmap,
			    struct inode *dir, struct dentry *dentry,
			    umode_t mode)
{
	struct inode *inode;
	int err;

	inode_inc_link_count(dir);

	inode = fortytwofs_new_inode(dir, S_IFDIR | mode, &dentry->d_name);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_dir;

	inode_inc_link_count(inode);
	err = ftfs_make_empty(inode, dir);
	if (err)
		goto out_fail;

	/*
	 * Clears the new flag from inode otherwise all other code waits on
	 * it to disappear which never happens and we get splats.
	 */
	d_instantiate_new(dentry, inode);
	return 0;
out_fail:
	/*
	 * Necessary because iput called by discard_new_inode spins
	 * while i_nlink > 0
	 */
	clear_nlink(inode);
	discard_new_inode(inode);
out_dir:
	inode_dec_link_count(dir);
	return err;
}

static int fortytwofs_create(struct mnt_idmap *idmap,
			     struct inode *dir, struct dentry *dentry,
			     umode_t mode, bool excl)
{
	(void)idmap;
	(void)excl;
	struct inode *inode = fortytwofs_new_inode(dir, mode, &dentry->d_name);

	if (IS_ERR(inode))
		return PTR_ERR(inode);
	d_instantiate_new(dentry, inode);
	return 0;
}

static struct dentry *fortytwofs_lookup(struct inode *dir,
					struct dentry *dentry,
					unsigned int flags)
{
	struct inode *inode = NULL;
	unsigned long ino;
	int err;

	if (dentry->d_name.len >= FT_MAX_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);
	err = ftfs_lookup_ino(dir, &dentry->d_name, &ino);
	if (!err) {
		inode = fortyfs_iget(dir->i_sb, ino);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	} else if (err != -ENOENT) {
		return ERR_PTR(err);
	}
	return d_splice_alias(inode, dentry);
}

const struct inode_operations fortytwofs_dir_inode_operations = {
	.create			= fortytwofs_create,
	.lookup			= fortytwofs_lookup,
	// .link		= fortytwofs_link,
	// .unlink		= fortytwofs_unlink,
	// .symlink		= fortytwofs_symlink,
	.mkdir			= fortytwofs_mkdir,
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
