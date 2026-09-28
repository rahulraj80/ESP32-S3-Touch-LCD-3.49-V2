#!/bin/bash
set -e
echo "=== Sourcing ESP-IDF ==="
. ~/scripts/esp-idf/export.sh
echo "=== Building 11_FactoryProgram ==="
cd ~/scripts/ws_v2/ESP-IDF/11_FactoryProgram
idf.py build
echo "=== Build Complete ==="
ls -lh build/*.bin
