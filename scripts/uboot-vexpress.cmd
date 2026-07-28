setenv zuzu_kernel_addr 0x80200000
setenv zuzu_initrd_addr 0x81000000
setenv zuzu_fdt_addr 0x83000000

load ${devtype} ${devnum}:${distro_bootpart} ${zuzu_kernel_addr} /boot/zuzu.uImage
load ${devtype} ${devnum}:${distro_bootpart} ${zuzu_initrd_addr} /boot/initrd.uImage
load ${devtype} ${devnum}:${distro_bootpart} ${zuzu_fdt_addr} /boot/vexpress-v2p-ca15-tc1.dtb

setenv fdt_high 0xffffffff
setenv initrd_high 0xffffffff
bootm ${zuzu_kernel_addr} ${zuzu_initrd_addr} ${zuzu_fdt_addr}
