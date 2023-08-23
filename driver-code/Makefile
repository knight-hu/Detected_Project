KERDIR = /lib/modules/$(shell uname -r)/build
obj-m += usb_monitor.o
CONFIG_MODULE_SIG=n
build:
	make -C $(KERDIR) M=$(CURDIR) modules

clean:
	make -C $(KERDIR) M=$(CURDIR) clean 
