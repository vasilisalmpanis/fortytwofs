#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_parser.h>
#include <linux/fs_context.h>
#include <linux/slab.h>

struct fortytwo_fs_context {
	kuid_t		s_resuid;
	kgid_t		s_resgid;
};

static int fortytwofs_get_tree(struct fs_context *fc)
{
	//return get_tree_bdev(fc, fortytwofs_fill_super);
	(void)fc;
	return 0;
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

	ctx = kzalloc_obj(*ctx);
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
MODULE_LICENSE("GPL3");
