#!/bin/sh
# Copy this project's files onto a clean Mayhem v2.4.0 tree.
# Usage: scripts/apply-overlay.sh /path/to/mayhem-firmware
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
dest=${1:-}
if [ -z "$dest" ] || [ ! -f "$dest/LICENSE" ]; then
    echo "usage: scripts/apply-overlay.sh /path/to/mayhem-firmware" >&2
    exit 1
fi
cp -a "$root/overlay/." "$dest/"
echo "Applied overlay onto $dest"
