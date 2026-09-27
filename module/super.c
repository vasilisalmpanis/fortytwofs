#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_parser.h>
#include <linux/fs_context.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/dcache.h>
#include "fortytwofs.h"
#include "ft_fs.h"

struct fortytwo_fs_context {
	kuid_t		s_resuid;
	kgid_t		s_resgid;
};

static int fortytwofs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	// struct fortytwo_fs_context *ctx = fc->fs_private;
	struct buffer_head *bh;
	struct inode *root;
	ft_super *super_ft = NULL;
	
	super_ft = kzalloc(sizeof(ft_super), GFP_KERNEL);
	if (super_ft == NULL)
		return -ENOMEM;
	if (sb_set_blocksize(sb, FT_BLOCK_SIZE) != FT_BLOCK_SIZE) {
		pr_err("fortytwofs: error: unable to set blocksize\n");
		kfree(super_ft);
		return -EINVAL;
	}
	if (!(bh = sb_bread(sb, 0))) {
		pr_err("fortytwofs: error: unable to read superblock\n");
		kfree(super_ft);
		return -EINVAL;
	}
	super_ft = (ft_super *)bh->b_data;
	if (super_ft->magic != FT_FS_MAGIC) {
		pr_err("fortytwofs: error: 42fs filesystem not found\n");
		kfree(super_ft);
		brelse(bh);
		return -EINVAL;
	}
	pr_info("Magic is correct\n");
	sb->s_magic = super_ft->magic;
	sb->s_fs_info = super_ft;
	sb->s_max_links = 0xFF;
	
	root = fortyfs_iget(sb, 0);
	if (IS_ERR(root)) {
		pr_err("fortytwofs: error: iget inode failed\n");
		kfree(super_ft);
		brelse(bh);
		return -EINVAL;
	}

	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		pr_err("fortytwofs: error: get root inode failed");
		kfree(super_ft);
		brelse(bh);
		return -ENOMEM;
	}
	brelse(bh);
	return (0);
}

static int fortytwofs_get_tree(struct fs_context *fc)
{
	return get_tree_bdev(fc, fortytwofs_fill_super);
}

static void fortytwofs_free_fc(struct fs_context *fc)
{
	kfree(fc->fs_private);
}

static const struct fs_context_operations fortytwofs_context_ops = {
	.parse_param	= NULL,
	.get_tree	= fortytwofs_get_tree,
	.reconfigure	= NULL,
	.free		= fortytwofs_free_fc,
};

static int fortytwofs_init_fs_context(struct fs_context *fc)
{
	struct fortytwo_fs_context *ctx = NULL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	fc->fs_private = ctx;
	fc->ops = &fortytwofs_context_ops;
	return 0;
}

static const struct fs_parameter_spec fortytwofs_param_spec[] = {
	{}
};

static struct file_system_type fortytwofs_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "fortytwofs",
	.kill_sb		= kill_block_super,
	.fs_flags		= FS_REQUIRES_DEV,
	.init_fs_context	= fortytwofs_init_fs_context,
	.parameters		= fortytwofs_param_spec,
};

static int __init fortytwofs_init(void)
{
	int err = 0;
	pr_info("Hello from fortytwofs\n");
	err = register_filesystem(&fortytwofs_fs_type);
	return err;
}
module_init(fortytwofs_init);

static void __exit fortytwofs_exit(void)
{
	pr_info("Goodbye from fortytwofs\n");
	unregister_filesystem(&fortytwofs_fs_type);
}
module_exit(fortytwofs_exit);

MODULE_AUTHOR("Vasileios Almpanis");
MODULE_AUTHOR("Polina Simonenko");
MODULE_DESCRIPTION("Fortytwofs, a filesystem from scratch");
MODULE_LICENSE("GPL v2");
