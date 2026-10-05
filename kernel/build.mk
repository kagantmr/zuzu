obj-y += boot_info.o
obj-$(CONFIG_ZUZU_BENCH) += bench.o
obj-y += kmain.o
obj-y += syspage.o

subdir-y += dev ipc irq loader mm sched space svc task time
