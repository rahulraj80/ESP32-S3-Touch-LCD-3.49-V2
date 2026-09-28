#!/bin/bash
set -e
echo "=== Sourcing ESP-IDF ==="
. ~/scripts/esp-idf/export.sh
echo "=== Building 09_LVGL_V8_Test ==="
cd ~/scripts/ws_v2/ESP-IDF/09_LVGL_V8_Test
idf.py build
echo "=== Build Complete ==="
ls -lh build/*.bin
