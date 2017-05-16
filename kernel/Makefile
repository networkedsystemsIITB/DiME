CONFIG_MODULE_SIG=n
CONFIG_MODULE_SIG_ALL=n

EXTRA_CFLAGS := -I./

obj-m += kmodule.o
kmodule-objs += da_mem_lib.o da_kmodule.o da_local_page_list.o da_ptracker.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
