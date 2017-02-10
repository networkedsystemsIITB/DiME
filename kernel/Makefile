CONFIG_MODULE_SIG=n
CONFIG_MODULE_SIG_ALL=n

EXTRA_CFLAGS := -I/home/kp/disaggregation/kernel/

obj-m += da_kmodule.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
