# kconfiglib (vendored)

Upstream: https://github.com/ulfalizer/Kconfiglib — version 14.1.0, ISC licensed.

Vendored so a fresh `git clone && gmake` works with no pip install. Only
`kconfiglib.py` (the library) and `menuconfig.py` (the terminal UI) are kept;
the upstream helper scripts are replaced by `scripts/kconf.py`.

To update: `pip download kconfiglib==<ver> --no-deps`, unzip, copy those two
files over, and update this file.
