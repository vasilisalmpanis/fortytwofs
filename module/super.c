// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

struct fortytwo_fs_context {
	kuid_t		s_resuid;
	kgid_t		s_resgid;
};

static const struct super_operations ftfs_sops = {
	.write_inode	= ftfs_write_inode,
	.evict_inode	= ftfs_evict_inode,
};

int ftfs_free_block(struct super_block *sb, u32 block)
{
	ft_super_info *super_info = sb->s_fs_info;
	ft_super *raw_super = super_info->super;
	u32 block_index = block / FT_BITMAP_CAPACITY_PER_BLOCK;
	u32 bit_to_unset = block % FT_BITMAP_CAPACITY_PER_BLOCK;
	struct buffer_head *bitmap_bh;

	if (block == 0 || block >= le32_to_cpu(raw_super->data.blocks_count))
		return -EINVAL;
	bitmap_bh = sb_bread(sb, block_index +
			le32_to_cpu(raw_super->data.blocks_bitmap_block));
	if (!bitmap_bh)
		return -EIO;

	mutex_lock(&super_info->lock);
	if (__test_and_clear_bit_le(bit_to_unset, bitmap_bh->b_data)) {
		le32_add_cpu(&raw_super->data.free_blocks, 1);
		mark_buffer_dirty(super_info->bh);
		mark_buffer_dirty(bitmap_bh);
	} else {
		pr_warn("freeing already free block %u\n", block);
	}
	mutex_unlock(&super_info->lock);
	brelse(bitmap_bh);
	return 0;
}

static int ftfs_check_super(struct super_block *sb, ft_super *raw)
{
	u32 inodes_count = le32_to_cpu(raw->data.inodes_count);
	u32 blocks_count = le32_to_cpu(raw->data.blocks_count);

	if (inodes_count == 0 || inodes_count > FT_MAX_INODES_COUNT) {
		pr_err("bad inodes_count %u\n", inodes_count);
		return -EINVAL;
	}
	if (blocks_count < FT_MIN_BLOCKS) {
		pr_err("bad blocks_count %u\n", blocks_count);
		return -EINVAL;
	}
	// TODO: some other checks
	return 0;
}

static int ftfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct buffer_head *bh;
	struct inode *root;
	int err;

	if (sb_set_blocksize(sb, FT_BLOCK_SIZE) != FT_BLOCK_SIZE) {
		pr_err("unable to set blocksize\n");
		return -EINVAL;
	}
	bh = sb_bread(sb, 0);
	if (!bh) {
		pr_err("unable to read superblock\n");
		return -EINVAL;
	}
	ft_super_info *super_ft __free(kfree) = kzalloc(sizeof(ft_super_info),
							GFP_KERNEL);
	if (!super_ft) {
		brelse(bh);
		return -ENOMEM;
	}

	super_ft->bh = bh;
	super_ft->super = (ft_super *)bh->b_data;
	mutex_init(&super_ft->lock);

	if (le32_to_cpu(super_ft->super->data.magic) != FT_FS_MAGIC) {
		pr_err("42fs filesystem not found\n");
		brelse(bh);
		return -EINVAL;
	}
	err = ftfs_check_super(sb, super_ft->super);
	if (err) {
		brelse(bh);
		return err;
	}
	sb->s_magic = FT_FS_MAGIC;
	sb->s_op = &ftfs_sops;
	sb->s_fs_info = no_free_ptr(super_ft);
	sb->s_max_links = 0xFF;

	root = fortyfs_iget(sb, 0);
	if (IS_ERR(root)) {
		pr_err("unable to get root inode\n");
		return PTR_ERR(root);
	}

	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		pr_err("unable to make root dentry\n");
		return -ENOMEM;
	}
	return 0;
}

static void ftfs_kill_sb(struct super_block *sb)
{
	ft_super_info *info = sb->s_fs_info;

	kill_block_super(sb);
	if (info) {
		brelse(info->bh);
		kfree(info);
	}
}

static int ftfs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, ftfs_fill_super);
}

static void ftfs_free_fc(struct fs_context *fc)
{
	kfree(fc->fs_private);
}

static const struct fs_context_operations ftfs_context_ops = {
	.parse_param	= NULL,
	.get_tree	= ftfs_get_tree,
	.reconfigure	= NULL,
	.free		= ftfs_free_fc,
};

static int ftfs_init_fs_context(struct fs_context *fc)
{
	struct fortytwo_fs_context *ctx = NULL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	fc->fs_private = ctx;
	fc->ops = &ftfs_context_ops;
	return 0;
}

int ftfs_alloc_new_block(struct super_block *sb)
{
	ft_super_info *info = (ft_super_info *)sb->s_fs_info;
	ft_super *ft_sb = info->super;
	struct buffer_head *bh = NULL;
	unsigned long bm_size = FT_BITMAP_CAPACITY_PER_BLOCK;
	int ret = 0;

	if (ft_sb->data.free_blocks == 0)
		return -ENOSPC;

	mutex_lock(&info->lock);
	int bm_blocks = ft_sb->data.blocks_count / FT_BITMAP_CAPACITY_PER_BLOCK;

	if (ft_sb->data.blocks_count % FT_BITMAP_CAPACITY_PER_BLOCK != 0)
		bm_blocks += 1;

	for (int bm_blk = 0; bm_blk < bm_blocks; bm_blk++) {
		bm_size = FT_BITMAP_CAPACITY_PER_BLOCK;
		if (bm_blk == bm_blocks - 1) {
			bm_size = ft_sb->data.blocks_count
				% FT_BITMAP_CAPACITY_PER_BLOCK;
			if (bm_size == 0)
				bm_size = FT_BITMAP_CAPACITY_PER_BLOCK;
		}
		bh = sb_bread(sb, bm_blk + ft_sb->data.blocks_bitmap_block);
		ret = find_next_zero_bit_le(bh->b_data, bm_size, 0);
		if (ret < bm_size) {
			__set_bit_le(ret, bh->b_data);
			mark_buffer_dirty(bh);
			brelse(bh);

			ft_sb->data.free_blocks -= 1;
			mark_buffer_dirty(info->bh);
			mutex_unlock(&info->lock);
			return FT_BITMAP_CAPACITY_PER_BLOCK * bm_blk + ret;
		}
		brelse(bh);
	}
	mutex_unlock(&info->lock);
	return -ENOSPC;
}

int ftfs_zalloc_new_block(struct super_block *sb)
{
	struct buffer_head *bh;
	int block = ftfs_alloc_new_block(sb);

	if (block < 0)
		return block;
	bh = sb_getblk(sb, block);
	if (!bh) {
		ftfs_free_block(sb, block);
		return -ENOMEM;
	}
	lock_buffer(bh);
	memset(bh->b_data, 0, FT_BLOCK_SIZE);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);
	mark_buffer_dirty(bh);
	brelse(bh);
	return block;
}

static const struct fs_parameter_spec ftfs_param_spec[] = {
	{}
};

static struct file_system_type ftfs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "fortytwofs",
	.kill_sb		= ftfs_kill_sb,
	.fs_flags		= FS_REQUIRES_DEV,
	.init_fs_context	= ftfs_init_fs_context,
	.parameters		= ftfs_param_spec,
};

static int __init fortytwofs_init(void)
{
	pr_info("Hello from fortytwofs\n");
	return register_filesystem(&ftfs_fs_type);
}
module_init(fortytwofs_init);

static void __exit fortytwofs_exit(void)
{
	pr_info("Goodbye from fortytwofs\n");
	unregister_filesystem(&ftfs_fs_type);
}
module_exit(fortytwofs_exit);

MODULE_AUTHOR("Vasileios Almpanis");
MODULE_AUTHOR("Polina Simonenko");
MODULE_DESCRIPTION("Fortytwofs, a filesystem from scratch");
MODULE_LICENSE("GPL v2");
