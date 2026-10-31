#!/bin/sh
# Installs the host for the current user: executable, user service and udev rule.
# The udev rule needs sudo; the rest does not.
set -e
cd "$(dirname "$(readlink -f "$0")")"
./build.sh
mkdir -p "$HOME/.local/bin" "$HOME/.config/systemd/user"
cp GalaxyNoteDisplay "$HOME/.local/bin/GalaxyNoteDisplay"
cp galaxynotedisplay.service "$HOME/.config/systemd/user/"
systemctl --user daemon-reload
sudo cp 70-galaxynotedisplay.rules /etc/udev/rules.d/
sudo udevadm control --reload
echo "Installed. Connect the tablet in External display mode: the monitor appears by itself."
