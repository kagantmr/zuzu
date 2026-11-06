obj-y += cache.o
obj-y += platform.o
# obj-y += context.o
obj-y += early.o
obj-y += bringup_stubs.o
# obj-y += vfp.o

subdir-y += boot exceptions
subdir-y += mmu
# subdir-y += irq timer
