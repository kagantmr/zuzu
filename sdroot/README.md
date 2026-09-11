# sdroot/

Files here are copied onto the SD card image as-is, before generated output is
staged on top. Tracked in git; put config files, test data, or anything else
the card should ship with here.

Generated staging lives in `build/<arch>-<board>/sdcard/` and is disposable.
