# Lutron for PortaPack Mayhem

Clear Connect Type A tools for a HackRF One with PortaPack Mayhem: a Pico receiver, and a transmitter that can emulate a Pico or send a dimmer set-level.

The firmware image in `release/` is Mayhem v2.4.0 plus the files in `overlay/`. It is GPL-3. Read `NOTICE.md` before you publish or redistribute it.

## What you get

| Path | What it is |
| --- | --- |
| `release/2026-10-14/portapack-mayhem_OCI.ppfw.tar` | SD card package. Extract it onto the card root so `FIRMWARE/` and `APPS/` come from this build. |
| `release/2026-10-14/portapack-mayhem-firmware.bin` | The same SPI image, if you flash from the host. |
| `release/2026-10-14/official-v2.4.0-rollback.bin` | Unmodified Mayhem v2.4.0, kept so this image can be undone. |
| `overlay/` | The Lutron files and the Mayhem files they change. |
| `patches/lutron-v2.4.0.patch` | The same change as one patch against tag `v2.4.0`. |

`SHA256SUMS` sits next to the binaries. The on-device version string is `2312cc5`. External apps are stamped `0x6f535ee6`, the first 8 hex digits of MD5(`2312cc5`). That stamp is the version string, so every image built from `2312cc5` carries it. Mayhem will treat an older Lutron app from this same version string as current. Copy `FIRMWARE` and every app from this tar, then flash this image.

Inside the tar the firmware file is `FIRMWARE/portapack-mayhem_dev.bin`.

## Install

1. Extract `portapack-mayhem_OCI.ppfw.tar` onto the SD card root. That writes `FIRMWARE/portapack-mayhem_dev.bin` and replaces the apps, including `APPS/lutron_rx.ppma` and `APPS/lutron_tx.ppma`. Leave `LUTRON/` alone if you already have aliases.
2. Flash `FIRMWARE/portapack-mayhem_dev.bin` from Utilities > Flash Utility, or flash `portapack-mayhem-firmware.bin` with the HackRF DFU stub and `hackrf_spiflash`. If the card already has `FIRMWARE/portapack-mayhem_v2.4.0.bin`, extract leaves that older file in place. Flash the `portapack-mayhem_dev.bin` from this tar.
3. To go back, flash `official-v2.4.0-rollback.bin` and restore an official app set.

Lutron RX and Lutron TX appear in the app menus after that pair is installed together.

## SD card files

Both apps use the folder `LUTRON/`:

- `aliases.txt` — eight hex digits, a space, then a name, for example `AABBCCDD Kitchen`. Later lines for the same serial win. The name is kept to 12 characters.
- `devices.txt` — one 8-hex serial per line. Lutron RX appends a Pico serial when it hears one. Lutron TX appends a serial you type.
- `toggles.txt` — `AABBCCDD 1`, the toggle bit the next Pico press of that serial will use. A press advances it. A release uses the previous bit and leaves the file as it is. Dimmer sends do not touch it.
- `subnet.txt` — six hex digits, the bridge header. Lutron RX appends a line when those three bytes change. Later lines win. The digits the app writes are uppercase.
- `saved.txt` — presses stored with Save in Lutron RX.
- `log.csv` — every burst, while the Log box in Lutron RX is ticked.

Device serials and the bridge header are not compiled into the firmware. They live in these files.

## Learn the bridge header

Open Lutron RX. It tunes to 433.610 MHz, the frequency that decoded the most bursts here. The bridge sends beacons while it is idle, and a beacon is enough. A bridge set-level is enough too. Pico button frames are skipped.

When RX accepts a frame, the console prints `SUBNET` and six hex digits, and it appends that line to `LUTRON/subnet.txt`. The info line gains `net` and the same digits. It writes again only when the three bytes change. If the card write fails, the console prints `SUBNET not saved` once.

## Transmit

Lutron TX opens at TX VGA gain 0 with the RF amp off, on 433.592 MHz. On the bench, Pico emulation was accepted at gain 8 with the amp on. Set those on the transmitter bar before START.

The top row is Prev, Next, Serial, and Name.

- Prev and Next walk `devices.txt` and `aliases.txt`.
- Serial asks for 8 hex digits and stores that id. Zero is rejected. A bad entry shows `serial is 8 hex digits`.
- Name stores an alias of up to 12 characters for the serial on screen.

`To` is `PICO` or `DIM`. `Lvl` is 0–100 and is used by DIM. `Btn`, `Act`, and `Rep` are used by PICO. Buttons are ON, OFF, RAISE, LOWER, and FAVORITE. Act is PRESS or RELEASE. Rep is the number of 75 ms repeats, default 14, maximum 20. The hint on screen is `DIM uses Lvl. START twice.`

Press START twice. The first press only arms. The status line starts with `again:`. Changing To, Lvl, Btn, Act, or Rep clears that. The second START sends. STOP aborts a send in progress.

**PICO** sends that remote's button. A dimmer responds when that Pico serial is one it already listens to.

**DIM** sends a set-level to the serial on screen. `Lvl` 0 is off and 100 is full on (`0xFEFF`), with a 250 ms fade. Other percents use percent × `0xFEFF` / 100. Switching to DIM shows `level`, the percent, and the six subnet digits, or `no subnet yet`. The armed line adds the percent and those digits, or `no subnet`.

If `subnet.txt` has no header yet, the second START does not transmit. The status line says `hear the bridge in Lutron RX first`. Open Lutron RX until it prints `SUBNET`, then open Lutron TX again.

## Serials in the source tree

The running firmware has no device list and no baked-in subnet. `overlay/firmware/test/baseband/lutron_test.cpp` is different: those tests compare built frames with packets captured on the air, so the serials and bridge-header bytes from those captures are in that file. Read it before you publish the repo if those values should stay private.

## Rebuild

The base is [mayhem-firmware](https://github.com/portapack-mayhem/mayhem-firmware) tag `v2.4.0`, commit `5c719645d6007bb3b0083ddff4863050d55fb8b5`.

```sh
git clone --branch v2.4.0 --depth 1 https://github.com/portapack-mayhem/mayhem-firmware.git
cd mayhem-firmware
git apply /path/to/lutron-portapack/patches/lutron-v2.4.0.patch
```

`scripts/apply-overlay.sh /path/to/mayhem-firmware` copies the same files without a patch. Build with Mayhem's own ARM gcc 9.2.1 docker image. The published stamp comes from the environment, because a fresh checkout of the tag would otherwise hash that tag:

```sh
VERSION_STRING=2312cc5
```

Set that before cmake. `VERSION_STRING` also changes the name of the firmware file inside the tar. The image in this release was built without `VERSION_STRING` set, in a tree whose `git log -n 1 --format=%h` was already `2312cc5`, so the file in the tar is `portapack-mayhem_dev.bin`.

Mayhem links the whole application with LTO. Any rebuild needs a new SPI image and a new copy of every app.

## License

See `NOTICE.md`. The short version: you can redistribute this, including the binaries, under GPL-3. You have to keep this overlay and the pointer to Mayhem v2.4.0 available with the binaries, and you cannot add terms that take those rights away.
