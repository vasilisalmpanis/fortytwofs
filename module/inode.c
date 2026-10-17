// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

static int ftfs_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	fileattr_fill_flags(fa, 0);
	return 0;
}

const struct inode_operations ftfs_symlink_inode_operations = {
	.get_link	= page_get_link,
};

static int get_lvl_idxs(__u32 logical_blk, __u32 (*idxs)[3])
{
	__u32 blks_lvl_1 = FT_PTRS_PER_BLOCK;
	__u32 blks_lvl_2 = blks_lvl_1 * FT_PTRS_PER_BLOCK;
	__u32 blks_lvl_3 = blks_lvl_2 * FT_PTRS_PER_BLOCK;
	__u32 rem = logical_blk;

	if (logical_blk >= blks_lvl_3)
		return -ENOSPC;
	(*idxs)[0] = logical_blk / blks_lvl_2;
	rem = rem - ((*idxs)[0] * blks_lvl_2);
	(*idxs)[1] = rem / blks_lvl_1;
	rem = rem - ((*idxs)[1] * blks_lvl_1);
	(*idxs)[2] = rem;
	return 0;
}

static int get_or_create_blk(struct inode *inode, sector_t logical_blk,
			     __u32 *res_blk, bool create, bool *new)
{
	ft_inode_info *info = inode->i_private;
	ft_inode *raw = info->inode;
	struct buffer_head *bh = NULL;
	__u32 idxs[3] = {0};
	int ret = 0;
	__u8 needed_lvl;
	__u32 *ptrs;
	__u32 blk;

	*res_blk = 0;
	ret = get_lvl_idxs(logical_blk, &idxs);
	if (ret)
		return ret;

	needed_lvl = idxs[0] ? 3 : idxs[1] ? 2 : idxs[2] ? 1 : 0;

	if (raw->level < needed_lvl) {
		if (!create)
			return 0;
		while (raw->level < needed_lvl) {
			ret = ftfs_next_level(inode);
			if (ret)
				return ret;
		}
	}

	if (raw->block == 0) {
		if (!create)
			return 0;
		ret = ftfs_zalloc_new_block(inode->i_sb);
		if (ret < 0)
			return ret;
		raw->block = ret;
		mark_buffer_dirty(info->bh);
		if (raw->level == 0)
			*new = true;
	}

	blk = raw->block;
	for (__u8 lvl = raw->level; lvl > 0; lvl--) {
		__u32 idx = idxs[3 - lvl];

		bh = sb_bread(inode->i_sb, blk);
		if (!bh)
			return -EIO;
		ptrs = (__u32 *)bh->b_data;
		if (ptrs[idx] == 0) {
			if (!create) {
				brelse(bh);
				return 0;
			}
			ret = ftfs_zalloc_new_block(inode->i_sb);
			if (ret < 0) {
				brelse(bh);
				return ret;
			}
			ptrs[idx] = ret;
			mark_buffer_dirty(bh);
			if (lvl == 1)
				*new = true;
		}
		blk = ptrs[idx];
		brelse(bh);
	}

	*res_blk = blk;
	return 0;
}

static int ftfs_get_or_create_block(struct inode *inode, sector_t logical_block,
				    int create, u32 *result_block, bool *new)
{
	ft_inode_info *info = inode->i_private;
	int ret;

	mutex_lock(&info->lock);
	ret = get_or_create_blk(inode, logical_block, result_block,
				create, new);
	mutex_unlock(&info->lock);
	return ret;
}

/**
 * @inode: Target inode
 * @block: logical block either to be read or written.
 * @bh_result: the variable that the buffer_head will be stored in.
 * @create: whether a new block should be allocated or not.
 */
static int ftfs_get_block(struct inode *inode, sector_t block,
			  struct buffer_head *bh_result, int create)
{
	bool new = false;
	__u32 blk = 0;
	int err;

	err = ftfs_get_or_create_block(inode, block, create, &blk, &new);
	if (err)
		return err;
	if (blk) {
		map_bh(bh_result, inode->i_sb, blk);
		if (new)
			set_buffer_new(bh_result);
	}
	return 0;
}

static int ftfs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, ftfs_get_block);
}

static int ftfs_read_folio(struct file *file, struct folio *folio)
{
	return block_read_full_folio(folio, ftfs_get_block);
}

static void ftfs_zero_post_eof(struct inode *inode, loff_t from)
{
	struct folio *folio;
	size_t off;

	folio = filemap_lock_folio(inode->i_mapping, from >> PAGE_SHIFT);
	if (IS_ERR(folio))
		return;
	off = offset_in_folio(folio, from);
	if (off)
		folio_zero_segment(folio, off, folio_size(folio));
	folio_unlock(folio);
	folio_put(folio);
}

static int ftfs_write_begin(struct file *file, struct address_space *mapping,
			    loff_t pos, unsigned int len,
			struct page **pagep, void **fsdata)
{
	struct inode *inode = mapping->host;
	loff_t isize = i_size_read(inode);
	int ret;

	if (pos > isize)
		ftfs_zero_post_eof(inode, isize);
	ret = block_write_begin(mapping, pos, len, pagep, ftfs_get_block);

	return ret;
}

static const struct address_space_operations ftfs_address_space_ops = {
	.dirty_folio		= block_dirty_folio,
	.invalidate_folio	= block_invalidate_folio,
	.read_folio		= ftfs_read_folio,
	.write_begin		= ftfs_write_begin,
	.write_end		= generic_write_end,
	.writepages		= ftfs_writepages,
	.direct_IO		= noop_direct_IO,
};

static const struct inode_operations fortyfs_inode_operations;

void ftfs_set_inode_ops(struct inode *inode, dev_t rdev)
{
	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &fortyfs_inode_operations;
		inode->i_fop = &ftfs_file_ops;
		inode->i_mapping->a_ops = &ftfs_address_space_ops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &ftfs_dir_inode_operations;
		inode->i_fop = &ftfs_dir_ops;
		inode->i_mapping->a_ops = &ftfs_address_space_ops;
	} else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &ftfs_symlink_inode_operations;
		inode_nohighmem(inode);
		inode->i_mapping->a_ops = &ftfs_address_space_ops;
	} else {
		init_special_inode(inode, inode->i_mode, rdev);
	}
}

static void ftfs_set_inode_data(struct inode *inode)
{
	ft_inode_info *info = (ft_inode_info *)inode->i_private;
	ft_inode *raw = info->inode;

	inode->i_mode = le16_to_cpu(raw->mode);
	i_uid_write(inode, le32_to_cpu(raw->uid));
	i_gid_write(inode, le32_to_cpu(raw->gid));
	set_nlink(inode, le16_to_cpu((__u16)raw->links));
	inode->i_size = le32_to_cpu(raw->size);
	inode->i_blocks = DIV_ROUND_UP(ALIGN(inode->i_size, FT_BLOCK_SIZE),
				       512);
	inode_set_atime(inode, (signed int)le32_to_cpu(raw->atime), 0);
	inode_set_ctime(inode, (signed int)le32_to_cpu(raw->ctime), 0);
	inode_set_mtime(inode, (signed int)le32_to_cpu(raw->mtime), 0);
	ftfs_set_inode_ops(inode, 0);
}

/* Take the first free inode number from the bitmap in the superblock */
static int ftfs_alloc_ino(struct super_block *sb, unsigned long *ino)
{
	ft_super_info *super_info = sb->s_fs_info;
	ft_super *super = super_info->super;
	u32 count = min_t(u32, le32_to_cpu(super->data.inodes_count),
			  FT_MAX_INODES_COUNT);
	unsigned long bit;
	int ret = -ENOSPC;

	mutex_lock(&super_info->lock);
	if (super->data.free_inodes == 0)
		goto out;
	bit = find_next_zero_bit_le(super->inodes_bitmap, count, 0);
	if (bit >= count)
		goto out;

	__set_bit_le(bit, super->inodes_bitmap);
	le32_add_cpu(&super->data.free_inodes, -1);
	mark_buffer_dirty(super_info->bh);
	*ino = bit;
	ret = 0;
out:
	mutex_unlock(&super_info->lock);
	return ret;
}

static void ftfs_free_ino(struct super_block *sb, unsigned long ino)
{
	ft_super_info *super_info = sb->s_fs_info;
	ft_super *super = super_info->super;

	mutex_lock(&super_info->lock);
	if (__test_and_clear_bit_le(ino, super->inodes_bitmap)) {
		le32_add_cpu(&super->data.free_inodes, 1);
		mark_buffer_dirty(super_info->bh);
	} else {
		pr_warn("freeing already free inode %lu\n", ino);
	}
	mutex_unlock(&super_info->lock);
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

static void ftfs_free_blocks(struct super_block *sb, __u32 block, __u8 lvl)
{
	struct buffer_head *bh;

	if (block == 0)
		return;
	bh = sb_bread(sb, block);
	if (!bh) {
		pr_warn("ftfs: can't read block during free");
		return;
	}
	if (lvl == 0) {
		if (ftfs_free_block(sb, block))
			pr_warn("ftfs: can't free block %d", block);
	} else {
		__u32 *blks = (__u32 *)bh->b_data;

		for (int i = 0; i < FT_PTRS_PER_BLOCK; i++) {
			if (blks[i] == 0)
				continue;

			ftfs_free_blocks(sb, blks[i], lvl - 1);
		}

		if (ftfs_free_block(sb, block))
			pr_warn("ftfs: can't free indirect block %d", block);
	}
	brelse(bh);
}

/**
 * ftfs_evict_inode evict and inode from cache
 *
 * Evict inode from cache. If number of links is 0
 * mark inode as free on superblock metadata.
 */
void ftfs_evict_inode(struct inode *inode)
{
	ft_inode *raw_inode;
	ft_inode_info *inode_info = inode->i_private;

	truncate_inode_pages_final(&inode->i_data);
	if (!inode_info) {
		clear_inode(inode);
		return;
	}
	raw_inode = inode_info->inode;

	/* Release the blocks */
	if (!inode->i_nlink) {
		ftfs_free_blocks(inode->i_sb, raw_inode->block, raw_inode->level);
		inode->i_size = 0;
	}

	clear_inode(inode);

	/* Mark inode bit as free in superblock bitmap */
	if (!inode->i_nlink) {
		ftfs_free_ino(inode->i_sb, inode->i_ino);

		/* Set inode emtpy in inodes block */
		memset(raw_inode, 0, sizeof(*raw_inode));
		mark_buffer_dirty(inode_info->bh);
	}

	/* Don't use bh after this point */
	brelse(inode_info->bh);
	inode_info->bh = NULL;
	kfree(inode_info);
}

int ftfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	ft_inode_info *info = inode->i_private;

	ftfs_write_raw_inode(info->inode, inode);
	mark_buffer_dirty(info->bh);

	if (wbc->sync_mode == WB_SYNC_ALL) {
		sync_dirty_buffer(info->bh);
		if (buffer_req(info->bh) && !buffer_uptodate(info->bh))
			return -EIO;
	}
	return 0;
}

struct inode *ftfs_new_inode(struct inode *dir, umode_t mode)
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
	mutex_init(&info->lock);

	error = ftfs_alloc_ino(sb, &ino);
	if (error)
		return ERR_PTR(error);

	bh = sb_bread(sb, ino / FT_INODES_PER_BLOCK + 1); /* First block is SB */
	if (!bh) {
		error = -EIO;
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
	ftfs_set_inode_ops(inode, 0);

	if (insert_inode_locked(inode) < 0) {
		error = -EIO;
		goto put_inode;
	}

	raw = (ft_inode *)bh->b_data + ino % FT_INODES_PER_BLOCK;
	memset(raw, 0, sizeof(*raw));
	ftfs_write_raw_inode(raw, inode);
	mark_buffer_dirty(bh);

	info->bh = bh;
	info->inode = raw;
	inode->i_private = no_free_ptr(info);
	return inode;

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
	mutex_init(&inode_info->lock);

	block = ino / FT_INODES_PER_BLOCK + 1;
	bh = sb_bread(sb, block);
	if (!bh) {
		pr_err("unable to read inode %lu\n", ino);
		iget_failed(inode);
		return ERR_PTR(-EIO);
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
		ftfs_free_block(inode->i_sb, block);
		return -EIO;
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

static bool ftfs_ptrs_empty(__u32 *ptrs)
{
	for (int i = 0; i < FT_PTRS_PER_BLOCK; i++)
		if (ptrs[i])
			return false;
	return true;
}

/**
 * Free every block at or after the position given by @idxs
 * Returns 1 if @blk is left with no pointers (the caller should free it),
 * Returns 0 if it still holds blocks,
 * Returns negative value in case of error.
 */
static int truncate_blk(struct super_block *sb, __u32 blk, __u8 lvl,
			__u32 (*idxs)[3])
{
	struct buffer_head	*bh;
	__u32			start = (*idxs)[3 - lvl];
	__u32			*ptrs;
	int			ret = 0;

	bh = sb_bread(sb, blk);
	if (!bh)
		return -EIO;
	ptrs = (__u32 *)bh->b_data;

	for (__u32 i = start + 1; i < FT_PTRS_PER_BLOCK; i++) {
		if (ptrs[i] == 0)
			continue;
		ftfs_free_blocks(sb, ptrs[i], lvl - 1);
		ptrs[i] = 0;
		mark_buffer_dirty(bh);
	}

	if (ptrs[start]) {
		if (lvl == 1)
			ret = 1;
		else
			ret = truncate_blk(sb, ptrs[start], lvl - 1, idxs);
		if (ret < 0)
			goto out;
		if (ret == 1) {
			if (ftfs_free_block(sb, ptrs[start]))
				pr_warn("ftfs: can't free block %d", ptrs[start]);
			ptrs[start] = 0;
			mark_buffer_dirty(bh);
		}
	}
	ret = ftfs_ptrs_empty(ptrs);
out:
	brelse(bh);
	return ret;
}

/* Free all the blocks starting from logical block @first */
static int ftfs_truncate_blocks(struct inode *inode, sector_t first)
{
	ft_inode_info	*info = inode->i_private;
	ft_inode	*raw = info->inode;
	__u32		idxs[3] = {0};
	int		ret = 0;
	__u8		needed_lvl;

	if (get_lvl_idxs(first, &idxs))
		return 0;
	needed_lvl = idxs[0] ? 3 : idxs[1] ? 2 : idxs[2] ? 1 : 0;

	mutex_lock(&info->lock);
	if (raw->block == 0 || raw->level < needed_lvl)
		goto out;

	if (raw->level == 0)
		ret = 1;
	else
		ret = truncate_blk(inode->i_sb, raw->block, raw->level, &idxs);
	if (ret == 1) {
		if (ftfs_free_block(inode->i_sb, raw->block))
			pr_warn("ftfs: can't free block %d", raw->block);
		raw->block = 0;
		raw->level = 0;
		mark_buffer_dirty(info->bh);
		ret = 0;
	}
out:
	mutex_unlock(&info->lock);
	return ret;
}

static int ftfs_setsize(struct inode *inode, loff_t new_size)
{
	int error;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) ||
	      S_ISLNK(inode->i_mode)))
		return -EINVAL;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return -EPERM;
	inode_dio_wait(inode);
	if (new_size > inode->i_size)
		ftfs_zero_post_eof(inode, inode->i_size);
	error = block_truncate_page(inode->i_mapping, new_size, ftfs_get_block);
	if (error)
		return error;

	filemap_invalidate_lock(inode->i_mapping);
	truncate_setsize(inode, new_size);
	error = ftfs_truncate_blocks(inode, DIV_ROUND_UP(new_size, FT_BLOCK_SIZE));
	filemap_invalidate_unlock(inode->i_mapping);
	if (error)
		return error;

	inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	mark_inode_dirty(inode);
	if (inode_needs_sync(inode))
		sync_inode_metadata(inode, 1);
	return 0;
}

static int ftfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
			struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	int error;

	error = setattr_prepare(&nop_mnt_idmap, dentry, attr);
	if (error)
		return error;

	if (attr->ia_valid & ATTR_SIZE && attr->ia_size != inode->i_size) {
		error = ftfs_setsize(inode, attr->ia_size);
		if (error)
			return error;
	}
	setattr_copy(&nop_mnt_idmap, inode, attr);
	mark_inode_dirty(inode);

	return 0;
}

static const struct inode_operations fortyfs_inode_operations = {
	// .listxattr		= ftfs_listxattr,
	// .getattr		= ftfs_getattr,
	.setattr		= ftfs_setattr,
	// .get_inode_acl	= ftfs_get_acl,
	// .set_acl		= ftfs_set_acl,
	// .fiemap		= ftfs_fiemap,
	.fileattr_get	= ftfs_fileattr_get,
	// .fileattr_set	= ftfs_fileattr_set,
};
