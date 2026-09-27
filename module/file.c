// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/byteorder/generic.h>
#include "fortytwofs.h"
#include "ft_fs.h"

static const struct file_operations fortytwofs_file_ops = {
	// .llseek	= generic_file_llseek,
	// .read_iter	= fortytwofs_read_iter,
	// .write_iter	= fortytwofs_write_iter,
	// .open	= fortytwofs_open,
};
