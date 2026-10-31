#!/bin/sh
# Builds the single GalaxyNoteDisplay executable (Python zipapp with pyusb included).
# System requirements: python3, python3-gi, GStreamer with pipewiresrc and x264enc, libusb.
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
echo "Created $(pwd)/GalaxyNoteDisplay"
