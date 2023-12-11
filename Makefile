obj-m = naivefs_.o
KVERSION = $(shell uname -r)
all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean

install:
	sudo insmod naivefs_.ko

uninstall:
	sudo rmmod naivefs_.ko