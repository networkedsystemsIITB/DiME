CONFIG_MODULE_SIG=n
CONFIG_MODULE_SIG_ALL=n
CONFIG_DEBUG_INFO=y

EXTRA_CFLAGS := -I./

obj-m += kmodule.o prp_fifo_module.o prp_lru_module.o prp_random_module.o
prp_fifo_module-objs += prp_fifo.o
prp_lru_module-objs += prp_lru.o
prp_random_module-objs += prp_random.o
kmodule-objs += da_mem_lib.o da_kmodule.o da_ptracker.o da_config.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
