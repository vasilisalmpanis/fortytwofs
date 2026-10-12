// SPDX-License-Identifier: GPL-2.0
#include "ft_fs.h"

const struct file_operations ftfs_file_ops = {
	.llseek		= generic_file_llseek,
	.write_iter	= generic_file_write_iter,
	.read_iter	= generic_file_read_iter,
	.mmap 		= generic_file_mmap,
	.fsync		= generic_file_fsync,
	.splice_read	= filemap_splice_read,
};
