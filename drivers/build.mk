ifneq ($(ARCH),aarch64)
obj-y += driver.o
subdir-y += uart
endif
