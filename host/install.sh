#!/bin/sh
# Installa l'host per l'utente corrente: eseguibile, servizio utente e regola udev.
# La regola udev richiede sudo; il resto no.
set -e
cd "$(dirname "$(readlink -f "$0")")"
./build.sh
mkdir -p "$HOME/.local/bin" "$HOME/.config/systemd/user"
cp GalaxyNoteDisplay "$HOME/.local/bin/GalaxyNoteDisplay"
cp galaxynotedisplay.service "$HOME/.config/systemd/user/"
systemctl --user daemon-reload
sudo cp 70-galaxynotedisplay.rules /etc/udev/rules.d/
sudo udevadm control --reload
echo "Installato. Collega il tablet in modalità Display esterno: il monitor compare da solo."
