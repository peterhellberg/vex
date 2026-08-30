#!/bin/sh
# build-icons.sh: res/icon.svg -> res/icon.png -> res/icon.ico
# Needs: inkscape, imagemagick. Run from the repo root.

set -e

inkscape res/icon.svg -w 512 -h 512 -o res/icon.png
echo "wrote res/icon.png"

if command -v magick > /dev/null 2>&1; then
  magick res/icon.png -define icon:auto-resize=256,128,64,48,32,24,16 res/icon.ico
elif convert -version 2> /dev/null | grep -q ImageMagick; then
  convert res/icon.png -define icon:auto-resize=256,128,64,48,32,24,16 res/icon.ico
else
  echo "error: no ImageMagick found" 1>&2
  exit 1
fi
echo "wrote res/icon.ico"

identify res/icon.ico
