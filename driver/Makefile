
CFILES := nvram_uio.c
obj-m := nvram_uio.o

KVERSION := $(shell uname -r)
umem_dev-objs := $(CFILES:.c=.o)

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean