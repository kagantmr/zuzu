obj-y += bsearch.o
obj-y += cpio.o
obj-y += mem.o
obj-y += snprintf.o
obj-y += string.o
obj-y += zxf.o

subdir-y += $(if $(wildcard klib/$(ARCH)/build.mk),$(ARCH))
