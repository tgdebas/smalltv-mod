#!/bin/sh
# Native tests for the Sonos response parsing (src/features/sonos/SonosXml.cpp,
# SonosParse.cpp). Stand-alone: its own minimal core shim, no libraries.
set -eu
cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/smalltv-sonos-parse-tests
c++ -std=c++11 -Wall -Wextra -Werror -Wno-unused-parameter -g -fsanitize=address,undefined \
  -Itests/sonos/shim -Isrc/features/sonos \
  tests/sonos/test_sonos_parse.cpp src/features/sonos/SonosXml.cpp src/features/sonos/SonosParse.cpp \
  -o "$OUT"
"$OUT"

# The /api/status member. ArduinoJson comes from the firmware build's libdeps,
# as for the other status tests.
JSON_INCLUDE=${THEME_JSON_INCLUDE:-.pio/libdeps/smalltv_esp32_8mb/ArduinoJson/src}
c++ -std=c++11 -Wall -Wextra -Werror -g -Isrc -I"$JSON_INCLUDE" \
  tests/sonos/test_status_sonos.cpp -o "$OUT-status"
"$OUT-status"
