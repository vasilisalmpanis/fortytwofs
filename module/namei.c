// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

static int ftfs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		      struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	int err;

	inode_inc_link_count(dir);

	inode = ftfs_new_inode(dir, S_IFDIR | mode, &dentry->d_name);
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

static int ftfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	int err = -ENOTEMPTY;

	err = ftfs_empty_dir(inode);
	if (err < 0)
		return err;
	if (err == 0)
		return -ENOTEMPTY;
	err = ftfs_remove_dentry(dir, &dentry->d_name);
	if (err < 0)
		return err;
	inode->i_size = 0;
	inode_set_ctime_to_ts(inode, inode_get_ctime(dir));
	inode_dec_link_count(inode);
	inode_dec_link_count(inode);
	inode_dec_link_count(dir);
	return 0;
}

static int ftfs_create(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, umode_t mode, bool excl)
{
	(void)idmap;
	(void)excl;
	struct inode *inode = ftfs_new_inode(dir, mode, &dentry->d_name);

	if (IS_ERR(inode))
		return PTR_ERR(inode);
	d_instantiate_new(dentry, inode);
	return 0;
}

static struct dentry *ftfs_lookup(struct inode *dir, struct dentry *dentry,
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

const struct inode_operations ftfs_dir_inode_operations = {
	.create			= ftfs_create,
	.lookup			= ftfs_lookup,
	// .link		= ftfs_link,
	// .unlink		= ftfs_unlink,
	// .symlink		= ftfs_symlink,
	.mkdir			= ftfs_mkdir,
	.rmdir			= ftfs_rmdir,
	// .mknod		= ftfs_mknod,
	// .rename		= ftfs_rename,
	// .listxattr		= ftfs_listxattr,
	// .getattr		= ftfs_getattr,
	// .setattr		= ftfs_setattr,
	// .get_inode_acl	= ftfs_get_acl,
	// .set_acl		= ftfs_set_acl,
	// .tmpfile		= ftfs_tmpfile,
	// .fileattr_get	= ftfs_fileattr_get,
	// .fileattr_set	= ftfs_fileattr_set,
};
