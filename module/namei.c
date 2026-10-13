// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

static int ftfs_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		      struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	int err;

	inode = ftfs_new_inode(dir, S_IFDIR | mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode_inc_link_count(inode);
	err = ftfs_make_empty(inode, dir);
	if (err)
		goto out_fail;

	err = ftfs_add_dentry(dir, inode, &dentry->d_name);
	if (err)
		goto out_fail;
	inode_inc_link_count(dir);

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
	struct inode *inode = ftfs_new_inode(dir, mode);
	int err;

	if (IS_ERR(inode))
		return PTR_ERR(inode);
	err = ftfs_add_dentry(dir, inode, &dentry->d_name);
	if (err) {
		clear_nlink(inode);
		discard_new_inode(inode);
		return err;
	}
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

static int ftfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	int err;

	err = ftfs_remove_dentry(dir, &dentry->d_name);
	if (err)
		return err;

	inode_set_ctime_to_ts(inode, inode_get_ctime(dir));
	inode_dec_link_count(inode);
	return 0;
}

static int ftfs_link(struct dentry *old_dentry, struct inode *dir,
		     struct dentry *new_dentry)
{
	struct inode *inode = d_inode(old_dentry);
	int err;

	inode_set_ctime_current(inode);
	inode_inc_link_count(inode);
	ihold(inode);

	err = ftfs_add_dentry(dir, inode, &new_dentry->d_name);
	if (err) {
		inode_dec_link_count(inode);
		iput(inode);
		return err;
	}

	d_instantiate(new_dentry, inode);
	return err;
}

static int ftfs_symlink(struct mnt_idmap *idmap, struct inode *dir,
			struct dentry *dentry, const char *linkname)
{
	struct inode *new_inode;
	int len = strlen(linkname);
	int err;

	if (len + 1 > FT_BLOCK_SIZE)
		return -ENAMETOOLONG;

	new_inode = ftfs_new_inode(dir, S_IFLNK | 0777);
	if (IS_ERR(new_inode))
		return PTR_ERR(new_inode);

	err = page_symlink(new_inode, linkname, len + 1);
	if (unlikely(err))
		goto out_fail;
	err = ftfs_add_dentry(dir, new_inode, &dentry->d_name);
	if (unlikely(err))
		goto out_fail;
	d_instantiate_new(dentry, new_inode);
	return 0;
out_fail:
	clear_nlink(new_inode);
	discard_new_inode(new_inode);
	return err;
}

static int ftfs_rename(struct mnt_idmap *idmap, struct inode *old_dir,
		       struct dentry *old_dentry, struct inode *new_dir,
		       struct dentry *new_dentry, unsigned int flags)
{
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode = d_inode(new_dentry);
	struct ft_dentry *old_parent_de = NULL;
	struct ft_dentry *old_de = NULL;
	struct buffer_head *bh_parent_de = NULL;
	struct buffer_head *bh_old_de = NULL;
	struct qstr dotdot = QSTR_INIT("..", 2);
	bool old_inode_was_dir = S_ISDIR(old_inode->i_mode);
	int ret;

	ret = ftfs_find_dentry(old_dir, &old_de, &bh_old_de,
			       &old_dentry->d_name);
	if (ret)
		return ret;
	if (old_inode_was_dir) {
		ret = ftfs_find_dentry(old_inode, &old_parent_de,
				       &bh_parent_de, &dotdot);
		if (ret)
			return ret;
	}

	if (new_inode) {
		ret = -ENOTEMPTY;
		if (old_inode_was_dir && !ftfs_empty_dir(new_inode))
			goto out_dir;
		ret = ftfs_remove_dentry(new_dir, &new_dentry->d_name);
		if (ret)
			goto out_dir;

		ret = ftfs_add_dentry(new_dir, old_inode, &new_dentry->d_name);
		if (ret)
			goto out_dir;

		if (old_inode_was_dir)
			drop_nlink(new_inode);
		inode_dec_link_count(new_inode);
	} else {
		ret = ftfs_add_dentry(new_dir, old_inode, &new_dentry->d_name);
		if (ret)
			goto out_dir;

		if (old_inode_was_dir)
			inode_inc_link_count(new_dir);
	}

	ret = ftfs_remove_dentry(old_dir, &old_dentry->d_name);
	if (!ret && old_inode_was_dir) {
		if (old_dir != new_dir) {
			old_parent_de->ino_idx = cpu_to_le32(new_dir->i_ino);
			mark_buffer_dirty(bh_parent_de);
		}
		inode_dec_link_count(old_dir);
	}
out_dir:
	if (bh_parent_de)
		brelse(bh_parent_de);
	if (bh_old_de)
		brelse(bh_old_de);
	return ret;
}

const struct inode_operations ftfs_dir_inode_operations = {
	.create			= ftfs_create,
	.lookup			= ftfs_lookup,
	.link			= ftfs_link,
	.unlink			= ftfs_unlink,
	.symlink		= ftfs_symlink,
	.mkdir			= ftfs_mkdir,
	.rmdir			= ftfs_rmdir,
	.rename			= ftfs_rename,
	// .mknod		= ftfs_mknod,
	// .listxattr		= ftfs_listxattr,
	// .getattr		= ftfs_getattr,
	// .setattr		= ftfs_setattr,
	// .get_inode_acl	= ftfs_get_acl,
	// .set_acl		= ftfs_set_acl,
	// .tmpfile		= ftfs_tmpfile,
	// .fileattr_get	= ftfs_fileattr_get,
	// .fileattr_set	= ftfs_fileattr_set,
};
