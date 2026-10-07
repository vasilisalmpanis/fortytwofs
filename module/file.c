// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

const struct file_operations ftfs_file_ops = {
	// .llseek	= generic_file_llseek,
	// .read_iter	= ftfs_read_iter,
	.write_iter = generic_file_write_iter,
	.read_iter = generic_file_read_iter,
	// .open	= ftfs_open,
};
