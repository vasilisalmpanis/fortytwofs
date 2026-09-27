# SPDX-License-Identifier: GPL-2.0
obj-m += fortytwofsmod.o
fortytwofsmod-objs := super.o inode.o dir.o file.o namei.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=${PWD} modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=${PWD} clean

.PHONY: all clean
