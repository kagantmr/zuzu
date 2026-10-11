# Out-of-tree projects (`EXT=`)

A product repo can carry zuzu as a submodule and add to it without patching it:

    make -C zuzu EXT=.. BOARD=<board>

`EXT` may contain:

| Path | Effect |
|---|---|
| `bsp/<arch>/<board>/` | Adds a board (same files as an in-tree BSP). Shadows an in-tree board of the same name. |
| `drivers/build.mk`, `drivers/Kconfig` | Extra kernel drivers, manifest format as in `drivers/`. |
| `Kconfig` | Extra config options, sourced from the root Kconfig. |
| `include/` | Added to the kernel include path. |
| `user/<group>/<prog>/build.mk` | Extra user programs. `user/newlib_apps/` builds with newlib. |

Objects go to `build/<arch>-<board>/ext/` and `.../user/ext/`. Without `EXT`
the build is unchanged.
