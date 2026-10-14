// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

#define FTFS_INFO_FORMAT "===\nInodes free: %u, total: %u\nBlocks free: %u, total: %u\n"
#define FTFS_INFO_HEADER "fortytwofs superblocks:\n"

struct misc_data {
	char	*buff;
	size_t	len;
};

static void handle_super(struct super_block *sb, void *file)
{
	struct file		*f = file;
	struct misc_data	*data;
	ft_super_info		*info;
	ft_super		*super;
	char			*new_buff;
	char			tmp_buff[100];
	ssize_t			len;

	data = f->private_data;
	if (!sb->s_fs_info)
		return;
	info = sb->s_fs_info;
	if (!info->super)
		return;
	super = info->super;
	len = snprintf(tmp_buff, 100,
		       FTFS_INFO_FORMAT,
		       super->data.free_inodes,
		       super->data.inodes_count,
		       super->data.free_blocks,
		       super->data.blocks_count);
	len += (data->len + 1);
	new_buff = kzalloc(len, GFP_KERNEL);
	if (!new_buff)
		return;
	if (data->len)
		memcpy(new_buff, data->buff, data->len);
	snprintf(new_buff + data->len, len - data->len,
		 FTFS_INFO_FORMAT,
		 super->data.free_inodes,
		 super->data.inodes_count,
		 super->data.free_blocks,
		 super->data.blocks_count);
	kfree(data->buff);
	data->buff = new_buff;
	data->len = len;
}

static int ftfs_misc_open(struct inode *inode, struct file *file)
{
	struct misc_data	*data;
	char			*header;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->len = 0;
	data->buff = NULL;
	file->private_data = data;

	header = kzalloc(sizeof(FTFS_INFO_HEADER) + 1, GFP_KERNEL);
	if (!header)
		return -ENOMEM;
	memcpy(header, FTFS_INFO_HEADER, sizeof(FTFS_INFO_HEADER));
	data->buff = header;
	data->len = sizeof(FTFS_INFO_HEADER);

	iterate_supers_type(&ftfs_fs_type, handle_super, file);
	return 0;
}

static int ftfs_misc_release(struct inode *inode, struct file *file)
{
	struct misc_data *data = file->private_data;

	if (data) {
		kfree(data->buff);
		kfree(data);
	}
	return 0;
}

static ssize_t ftfs_misc_read(struct file *file, char __user *buff,
			      size_t len, loff_t *off)
{
	struct misc_data	*data = file->private_data;
	const char		*src = data && data->buff ? data->buff : "";
	size_t			src_len = data ? data->len : 0;

	return simple_read_from_buffer(buff, len, off, src, src_len);
}

static const struct file_operations ftfs_misc_dev_fops = {
	.owner		= THIS_MODULE,
	.open		= ftfs_misc_open,
	.read		= ftfs_misc_read,
	.release	= ftfs_misc_release,
};

struct miscdevice ftfs_misc_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "ftfs",
	.fops	= &ftfs_misc_dev_fops,
	.mode	= 0666,
};
