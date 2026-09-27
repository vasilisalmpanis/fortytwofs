#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/byteorder/generic.h>
#include "fortytwofs.h"
#include "ft_fs.h"

const struct inode_operations fortyfs_inode_operations = {
    0
	// .listxattr	= fortytwofs_listxattr,
	// .getattr	= fortytwofs_getattr,
	// .setattr	= fortytwofs_setattr,
	// .get_inode_acl	= fortytwofs_get_acl,
	// .set_acl	= fortytwofs_set_acl,
	// .fiemap		= fortytwofs_fiemap,
	// .fileattr_get	= fortytwofs_fileattr_get,
	// .fileattr_set	= fortytwofs_fileattr_set,
};


struct inode *fortyfs_iget(struct super_block *sb, unsigned long ino)
{
    struct inode *inode;
    struct buffer_head *bh = NULL;
    ft_inode *inode_ft = {0};
    ft_super *super_ft = (ft_super *)sb->s_fs_info;
    int block;
    int ino_in_block;

    if (ino >= super_ft->inodes_count) {
        return  ERR_PTR(-EINVAL);
    }
    
    inode = iget_locked(sb, ino);
	if (!inode)      
        return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
        return inode;
    
    inode_ft = kzalloc(sizeof(ft_inode), GFP_KERNEL);
    if (!inode_ft) {
        iget_failed(inode);
        return  ERR_PTR(-ENOMEM);
    }

    block = ino / FT_INODES_PER_BLOCK + 1;
    if (!(bh = sb_bread(sb, block))) {
		pr_err("fortytwofs: error: unable to read inode\n");
		iget_failed(inode);
        kfree(inode_ft);
		return  ERR_PTR(-EINVAL);
	}
    ino_in_block = ino % FT_INODES_PER_BLOCK;
    memcpy(inode_ft, bh->b_data + ino_in_block * sizeof(ft_inode), sizeof(ft_inode));
    inode->i_private = inode_ft;

    inode->i_mode = le16_to_cpu(inode_ft->mode);
    i_uid_write(inode, le32_to_cpu(inode_ft->uid));
    i_gid_write(inode, le32_to_cpu(inode_ft->gid));
    set_nlink(inode, le16_to_cpu((__u16)inode_ft->links));
    inode->i_size = le32_to_cpu(inode_ft->size);
    inode_set_atime(inode, (signed)le32_to_cpu(inode_ft->atime), 0);
    inode_set_ctime(inode, (signed)le32_to_cpu(inode_ft->ctime), 0);
    inode_set_mtime(inode, (signed)le32_to_cpu(inode_ft->mtime), 0);
    if (inode->i_nlink == 0 && inode->i_mode == 0) {
        iget_failed(inode);
        kfree(inode_ft);
        return  ERR_PTR(-ESTALE);
    }


    inode->i_op = &fortyfs_inode_operations;
    if (S_ISREG(inode->i_mode)) {
        inode->i_fop = &fortytwofs_file_ops;
    } else if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &fortytwofs_dir_inode_operations;
        inode->i_fop = &fortytwofs_dir_ops;
    }
    brelse(bh);
    unlock_new_inode(inode);
    return inode;
};
