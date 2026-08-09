#ifndef ZUZU_BOOT_H
#define ZUZU_BOOT_H

/* Fixed slot in sysd's own handle table where the kernel seeds devmgr's
 * HANDLE_TASK entry at boot, so sysd can SysKickstart devmgr without ever
 * calling SysPSpawn for it. Slot 0 is reserved by convention for the
 * nameserver port sysd creates itself, as the very first handle allocation
 * in its own init. Must be >= 4: SysPSpawn blindly copies the spawning
 * parent's slots 0-3 into every child it creates (intentional for slot 0,
 * so nameserver's port propagates to every process sysd spawns) so a slot
 * inside that range would leak a HANDLE_TASK capability over devmgr
 * (kickstart/kill) to every child sysd spawns, not just sysd itself. */
#define SYSD_DEVMGR_TASK_HANDLE_SLOT 4

/* devmgr's pid is an emergent property of boot order (kernel loads sysd
 * first via boot.manifest's "init" entry, devmgr second via "dev"), not an
 * enforced invariant,  same convention NAMETABLE_PID relied on for sysd's
 * pid. Kept as an explicit constant so sysd's own devmgr-targeted SysGrant
 * doesn't repeat the bare literal. */
#define DEVMGR_PID 2

#endif
