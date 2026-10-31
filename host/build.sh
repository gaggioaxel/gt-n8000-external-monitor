#!/bin/sh
# Crea l'eseguibile unico GalaxyNoteDisplay (zipapp Python con pyusb incluso).
# Richiede sul sistema: python3, python3-gi, GStreamer con pipewiresrc e x264enc, libusb.
set -e
cd "$(dirname "$(readlink -f "$0")")"
rm -rf build && mkdir build
/usr/bin/python3 -m pip install --quiet --no-compile --target build pyusb
rm -rf build/*.dist-info build/bin
cp -r galaxynotedisplay build/
find build -name __pycache__ -prune -exec rm -rf {} +
printf 'from galaxynotedisplay.app import main\nmain()\n' > build/__main__.py
/usr/bin/python3 -m zipapp build -p /usr/bin/python3 -c -o GalaxyNoteDisplay
rm -rf build
echo "Creato $(pwd)/GalaxyNoteDisplay"
