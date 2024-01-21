obj-m = naivefs_.o
KVERSION = $(shell uname -r)
all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules
	sudo insmod naivefs_.ko
	mount -t naivefs none /mnt

clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean

install:
	sudo insmod naivefs_.ko
	mount -t naivefs none /mnt

uninstall:
	sudo umount /mnt
	sudo rmmod naivefs_.ko