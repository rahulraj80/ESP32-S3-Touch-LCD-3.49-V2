#!/bin/bash
set -e
. ~/scripts/esp-idf/export.sh
cd ~/scripts/ws_v2/ESP-IDF/10_LVGL_V9_Test
idf.py build
ls -lh build/*.bin
