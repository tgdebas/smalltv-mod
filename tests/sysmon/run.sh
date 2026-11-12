#!/bin/sh
# Native tests for the Mac stats screen's data (src/features/sysmon) and the
# parsers of the Mac script (tools/mac/smalltv-stats.sh). ArduinoJson comes
# from the firmware build's libdeps, as for the other status tests.
set -eu
cd "$(dirname "$0")/../.."
JSON_INCLUDE=${THEME_JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
OUT=${TMPDIR:-/tmp}/smalltv-sysmon-tests
c++ -std=c++11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc -Isrc/features/sysmon -I"$JSON_INCLUDE" \
  tests/sysmon/test_sysmon.cpp src/features/sysmon/SysmonData.cpp -o "$OUT"
"$OUT"
bash tests/sysmon/test_mac_script.sh
