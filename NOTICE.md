# License notice

This is not legal advice. It is the notice that goes with this tree so a recipient can see what they received.

## The binary is a Mayhem firmware

`release/2026-10-14/portapack-mayhem-firmware.bin` and the matching `portapack-mayhem_OCI.ppfw.tar` are a modified [PortaPack Mayhem](https://github.com/portapack-mayhem/mayhem-firmware) v2.4.0 image (tag `v2.4.0`, commit `5c719645d6007bb3b0083ddff4863050d55fb8b5`), built 2026-10-14. The version string inside the image is `2312cc5`.

Mayhem's `LICENSE` is the GNU General Public License version 3. A copy is `LICENSE` in this folder. Conveying the SPI image or the `.ppma` / `.ppmp` apps conveys that whole program, not a separate Lutron program. GPL-3 requires the corresponding source of the whole image: the Mayhem tag above, plus every change in `overlay/` (the same change is `patches/lutron-v2.4.0.patch`). Both have to stay available for as long as you offer the binaries. This folder is that offer. A link to upstream Mayhem alone is not enough, because upstream does not contain these changes, and you are responsible if the upstream tag disappears.

Do not relicense the firmware, the apps, or this overlay under MIT, BSD, Apache-only, or a "no commercial use" term. Those terms would conflict with GPL-3. You may charge for a board or for a download. You may not stop the recipient from rebuilding, modifying, and passing it on under GPL-3.

`official-v2.4.0-rollback.bin` is an unmodified Mayhem v2.4.0 image, kept here so the modified image can be removed. Its source is the upstream tag, not this overlay.

## Your files

Copyright (C) 2026 Dave Hammer.

New files in `overlay/` are GPL-3 or any later version, as their headers say. `firmware/test/baseband/lutron_test.cpp` says GPL-2 or any later version. GPL-3 is one of those later versions, so it sits in this distribution under GPL-3.

## Files that already had a license

These upstream files are modified. The modification is dated 2026-10-14. Their original notices stay in the files:

- `firmware/common/message.hpp` and `firmware/common/spi_image.hpp` — GPL-2 or any later version.
- `firmware/application/baseband_api.cpp`, `firmware/application/baseband_api.hpp`, `firmware/baseband/CMakeLists.txt`, `firmware/test/baseband/CMakeLists.txt`, `firmware/tools/external_app_info.py`, `firmware/application/external/external.cmake` — Mayhem sources, covered by the project's GPL-3.
- `firmware/application/external/external.ld` — Apache License 2.0, copyright Bernd Herzog and Mark Thompson. The Apache header in that file stays. A copy of the Apache License is `licenses/Apache-2.0.txt`. Apache-2.0 code may be combined into a GPL-3 program. This file is part of the GPL-3 firmware, and the Apache notice is retained.

## HackRF

The SPI image also contains HackRF baseband code. In this Mayhem tree those files are copyright Great Scott Gadgets and Jared Boone and say GPL-2 or any later version (`hackrf/COPYING` is the GPL-2 text; a copy is `licenses/GPL-2.0.txt`). "Or any later version" is what lets that code be built into Mayhem's GPL-3 firmware. Do not strip the HackRF copyright notices when you redistribute source.

## What "without the Mayhem tree" does and does not allow

Publishing this folder, and not a full fork, is fine. The folder holds your changes, the GPL-3 text, and the binaries. It does not hold a second copy of Mayhem.

It is not fine to publish only the `.bin` and the `.ppma` files. Recipients of those binaries are entitled to the corresponding source of the whole firmware. It is also not fine to publish the Lutron screens as if they were a separate permissively licensed app: they are linked into the Mayhem application, including its LTO build, and the `.ppma` is extracted from that binary.

If you later build from a different commit, publish a new patch or overlay for that commit with the new binaries. The source offer has to match the image you are shipping.
