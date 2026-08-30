#!/bin/sh
# cmd/vex/res/icon.svg -> icon.png -> icon.ico -> icon.h (run from repo root)
set -e
inkscape cmd/vex/res/icon.svg -w 512 -h 512 -o cmd/vex/res/icon.png
echo "wrote cmd/vex/res/icon.png"
if command -v magick >/dev/null 2>&1; then magick cmd/vex/res/icon.png -define icon:auto-resize=256,128,64,48,32,24,16 cmd/vex/res/icon.ico; else convert cmd/vex/res/icon.png -define icon:auto-resize=256,128,64,48,32,24,16 cmd/vex/res/icon.ico; fi
echo "wrote cmd/vex/res/icon.ico"
{
  echo "// generated from res/icon.png via build-icons.sh; do not edit"
  echo "#pragma once"
  xxd -i cmd/vex/res/icon.png | sed -e 's/cmd_vex_res_icon_png/vex_icon_png/g' -e 's/unsigned/static const unsigned/'
} > cmd/vex/res/icon.h
echo "wrote cmd/vex/res/icon.h"
