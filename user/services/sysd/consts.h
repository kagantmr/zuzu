#ifndef SYSD_CONSTS_H
#define SYSD_CONSTS_H

#define SYSD_VERSION "v4.0"

#define SYSD_NAME_LEN 4

/* CPIO name of nameserver's ELF, looked up directly against the initrd
 * sysd already has mapped — bypasses boot.manifest entirely, since fsd
 * (and its exec-IPC protocol) doesn't exist yet this early in boot. */
#define NAMESERVER_ELF_PATH "bin/nameserver"

#endif