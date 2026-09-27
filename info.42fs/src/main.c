#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../../mkfs.42fs/fortytwofs.h"

int main(int ac, char **av) {
    struct stat statbuf;
    int         fs_fd;
    ft_super    s;
    ft_inode    i;
    ft_dentry   d;

    if (ac < 2) {
        fprintf(stderr, "Usage: %s [path to image]\n", av[0]);
        exit(1);
    }
    if (stat(av[1], &statbuf) < 0) {
        perror(av[0]);
        exit(1);
    }
    if (!(S_ISBLK(statbuf.st_mode) || S_ISREG(statbuf.st_mode))) {
        fprintf(stderr, "%s: not a regular file or block device\n", av[0]);
        exit(1);
    }
    if (S_ISREG(statbuf.st_mode) && statbuf.st_size < 4 * FT_BLOCK_SIZE) {
        fprintf(stderr, "%s: %s: size of image is too small\n", av[0], av[1]);
        exit(1);
    }

    fs_fd = open(av[1], O_RDONLY);
    if (fs_fd < 0) {
        perror(av[0]);
        exit(1);
    }
    
    // Read superblock
    if (read(fs_fd, &s, sizeof(s)) < 0) {
        perror(av[0]);
        exit(1);
    }
    if (s.magic != FT_FS_MAGIC) {
        fprintf(stderr, "%s: %s magic does not match\n", av[0], av[1]);
        exit(1);
    }

    printf("Blocks: %d / %d\n",  s.free_blocks, s.blocks_count);
    printf("Inodes: %d / %d\n",  s.free_inodes, s.inodes_count);
    printf("Taken: %d B, free: %d B, total: %d B\n",
        (s.blocks_count - s.free_blocks) * FT_BLOCK_SIZE,
        s.free_blocks * FT_BLOCK_SIZE,
        s.blocks_count * FT_BLOCK_SIZE
    );

    // Read root inode
    lseek(fs_fd, FT_BLOCK_SIZE, SEEK_SET);
    if (read(fs_fd, &i, sizeof(i)) < 0) {
        perror(av[1]);
        exit(1);
    }
    printf(
"Root inode:\n\
  uid:  \t%d\n\
  gid:  \t%d\n\
  size: \t%d\n\
  mode: \t%o\n\
  ctime:\t%d\n\
  mtime:\t%d\n\
  atime:\t%d\n\
  level:\t%d\n\
  block:\t%d\n",
        i.uid, i.gid, i.size, i.mode, i.ctime, i.mtime, i.atime, i.level, i.block
    );

    // Read root inode dentries
    lseek(fs_fd, i.block * FT_BLOCK_SIZE, SEEK_SET);
    size_t bytes_read = 0;
    ssize_t oneshot_read = 0;
    while (bytes_read < FT_BLOCK_SIZE) {
        memset(&d, 0, sizeof(d));
        oneshot_read = read(fs_fd, &d, sizeof(d));
        if (oneshot_read < 0) {
            perror(av[1]);
            exit(1);
        }
        bytes_read += oneshot_read;
        if (d.type == FT42_FREE)
            continue ;
        printf(
            "Dentry: type: %d, inode: %d, name: %s\n",
            d.type, d.ino_idx, d.name
        );
    }
    close(fs_fd);
    return(0);
}
