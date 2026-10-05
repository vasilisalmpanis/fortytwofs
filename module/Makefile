# SPDX-License-Identifier: GPL-2.0
obj-m += fortytwofsmod.o
fortytwofsmod-objs := super.o inode.o dir.o file.o namei.o

KDIR := /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(CURDIR) modules
clean:
	make -C $(KDIR) M=$(CURDIR) clean

.PHONY: all clean
