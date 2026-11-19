#!/bin/bash
# smalltv-stats.sh — sends this Mac's CPU, memory, disk and network use to a
# SmallTV running smalltv-mod, for its "Mac stats" screen.
#
#   bash smalltv-stats.sh install HOST   start now and at every login (LaunchAgent)
#   bash smalltv-stats.sh uninstall      stop and remove the LaunchAgent
#   bash smalltv-stats.sh run HOST       send every 3 s in this Terminal (Ctrl+C stops)
#   bash smalltv-stats.sh print          show the numbers once, send nothing
#
# HOST is the device's address, e.g. smalltv-1a2b.local or 192.168.1.50.
# Only tools that ship with macOS are used: top, vm_stat, sysctl, df, route,
# netstat and curl. Nothing is installed besides the LaunchAgent.
#
# What is sent: POST http://HOST/api/sysmon with cpu, mem, disk (percent) and
# rx, tx (Mbit/s on the default network interface).
#
# Covers: when the SmallTV shows Sonos Now Playing but cannot fetch the cover
# itself (an https-only image, a PNG, or not enough memory), its reply names
# the image. The script then downloads it, shrinks it to 160 px and converts it
# to BMP with sips (built into macOS), and sends it to http://HOST/api/sonos/art.
# BMP, not JPEG: sips keeps a progressive source progressive, which the
# device's JPEG decoder cannot read; it converts a BMP itself as it arrives.
# Each cover is fetched once. Set SMALLTV_STATS_COVERS=0 to turn this off.

set -u

LABEL="com.smalltv-mod.stats"
INTERVAL=3                       # seconds between pushes (top's sample takes 1 of them)
SUPPORT_DIR="$HOME/Library/Application Support/smalltv-stats"
AGENT="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/smalltv-stats.log"

# ---- parsers: read a command's output on stdin, print one number ---------------
# Kept separate from the commands so tests/sysmon can feed them recorded output.

# top -l 2 -n 0 -s 1: "CPU usage: 7.69% user, 10.25% sys, 82.5% idle" (the
# second sample is the one measured over the interval). Prints busy percent.
parse_cpu() {
  awk '/^CPU usage:/ { line = $0 }
       END {
         if (line == "") exit 1
         n = split(line, f, " ")
         for (i = 1; i <= n; i++) if (f[i] == "idle") { idle = f[i-1]; sub(/%,?/, "", idle) }
         printf "%.1f\n", 100 - idle
       }'
}

# vm_stat, plus the total memory in bytes as $1. Prints used percent the way
# Activity Monitor counts "Memory Used": app memory + wired + compressed, where
# app memory is anonymous pages minus purgeable ones (older macOS: active).
parse_mem() {
  awk -v total="$1" '
    /page size of/ { for (i = 1; i <= NF; i++) if ($i == "of") ps = $(i+1) }
    function val() { v = $NF; sub(/\.$/, "", v); return v + 0 }
    /^Anonymous pages:/                 { anon = val(); hasAnon = 1 }
    /^Pages purgeable:/                 { purge = val() }
    /^Pages active:/                    { active = val() }
    /^Pages wired down:/                { wired = val() }
    /^Pages occupied by compressor:/    { comp = val() }
    END {
      if (ps == 0 || total == 0) exit 1
      app = hasAnon ? anon - purge : active
      if (app < 0) app = 0
      printf "%.1f\n", 100 * (app + wired + comp) * ps / total
    }'
}

# df -k on the data volume. On APFS the volumes share one container, so used
# space is size minus what is still available, like Finder shows it.
parse_disk() {
  awk 'NR == 2 { size = $2; avail = $4 }
       END { if (size == 0) exit 1; printf "%.1f\n", 100 * (size - avail) / size }'
}

# netstat -ibn -I IFACE: the <Link#n> row holds the byte counters. Counted
# from the end of the line, because the Address column can be empty.
# Prints "IN_BYTES OUT_BYTES".
parse_net() {
  awk '/<Link#/ { print $(NF-4), $(NF-1); found = 1; exit } END { if (!found) exit 1 }'
}

# ---- samplers -------------------------------------------------------------------------
sample_cpu()  { top -l 2 -n 0 -s 1 2>/dev/null | parse_cpu; }
sample_mem()  { vm_stat 2>/dev/null | parse_mem "$(sysctl -n hw.memsize 2>/dev/null)"; }
sample_disk() {
  local vol=/System/Volumes/Data
  [ -d "$vol" ] || vol=/
  df -k "$vol" 2>/dev/null | parse_disk
}
default_iface() { route -n get default 2>/dev/null | awk '/interface:/ { print $2; exit }'; }
sample_net_bytes() {
  local ifc
  ifc=$(default_iface)
  [ -n "$ifc" ] || return 1
  netstat -ibn -I "$ifc" 2>/dev/null | parse_net
}

# Mbit/s from two byte counters taken SECONDS apart; a counter that went
# backwards (interface reset, sleep) reads as 0.
rate_mbps() {   # OLD NEW SECONDS
  awk -v a="$1" -v b="$2" -v s="$3" 'BEGIN {
    d = b - a; if (d < 0 || s <= 0) d = 0
    printf "%.2f\n", d * 8 / 1000000 / s }'
}

# ---- sending -------------------------------------------------------------------------------
# Prints the device's reply, which may name a cover it wants (see below).
send() {   # HOST CPU MEM DISK RX TX
  curl -fsS -m 4 \
    --data "cpu=$2&mem=$3&disk=$4&rx=$5&tx=$6" "http://$1/api/sysmon"
}

# The reply when the SmallTV wants a cover: {"ok":true,"art":"<url>","key":"<8 hex>"}
reply_art() { sed -n 's/.*"art"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | sed 's|\\/|/|g'; }
reply_key() { sed -n 's/.*"key"[[:space:]]*:[[:space:]]*"\([0-9a-f]\{8\}\)".*/\1/p'; }

# Download a cover, make it a 160 px BMP, send it to the device.
relay_cover() {   # HOST URL KEY
  local tmp rc=1
  tmp=$(mktemp -d) || return 1
  if curl -fsSL -m 15 -o "$tmp/in" "$2" &&
     sips -s format bmp -Z 160 "$tmp/in" --out "$tmp/out.bmp" >/dev/null 2>&1 &&
     curl -fsS -m 20 -o /dev/null -F "file=@$tmp/out.bmp;type=image/bmp" \
       "http://$1/api/sonos/art?key=$3"; then
    rc=0
  fi
  rm -rf "$tmp"
  return $rc
}

run_loop() {
  local host=$1 prev_in="" prev_out="" prev_t="" warned=0
  local cover_sent="" cover_sent_at=0 covers=${SMALLTV_STATS_COVERS:-1}
  echo "Sending this Mac's stats to $host every ${INTERVAL}s (Ctrl+C stops)."
  while :; do
    local cpu mem disk rx=0 tx=0 bytes now
    cpu=$(sample_cpu) || cpu=""
    mem=$(sample_mem) || mem=""
    disk=$(sample_disk) || disk=""
    now=$(date +%s)
    if bytes=$(sample_net_bytes); then
      set -- $bytes
      if [ -n "$prev_t" ] && [ "$now" -gt "$prev_t" ]; then
        rx=$(rate_mbps "$prev_in" "$1" $((now - prev_t)))
        tx=$(rate_mbps "$prev_out" "$2" $((now - prev_t)))
      fi
      prev_in=$1 prev_out=$2 prev_t=$now
    fi
    local reply
    if reply=$(send "$host" "$cpu" "$mem" "$disk" "$rx" "$tx" 2>/dev/null); then
      warned=0
      local key url
      key=$(printf '%s' "$reply" | reply_key)
      # The device asks only while it lacks the cover: a new one is sent at
      # once, the same one again (it could not use the copy) once a minute.
      if [ "$covers" = 1 ] && [ -n "$key" ] &&
         { [ "$key" != "$cover_sent" ] || [ $((now - cover_sent_at)) -ge 60 ]; }; then
        url=$(printf '%s' "$reply" | reply_art)
        cover_sent=$key cover_sent_at=$now
        if [ -z "$url" ] || ! relay_cover "$host" "$url" "$key"; then
          echo "$(date '+%F %T') could not relay cover $url" >&2
        fi
      fi
    elif [ $warned -eq 0 ]; then
      echo "$(date '+%F %T') could not reach $host; retrying every ${INTERVAL}s" >&2
      warned=1
    fi
    sleep $((INTERVAL - 1))   # top already took about a second
  done
}

install_agent() {
  local host=$1
  mkdir -p "$SUPPORT_DIR" "$(dirname "$AGENT")" "$(dirname "$LOG")"
  cp "$0" "$SUPPORT_DIR/smalltv-stats.sh"
  cat > "$AGENT" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>/bin/bash</string>
    <string>$SUPPORT_DIR/smalltv-stats.sh</string>
    <string>run</string>
    <string>$host</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ProcessType</key><string>Background</string>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict>
</plist>
PLIST
  launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null
  if launchctl bootstrap "gui/$(id -u)" "$AGENT"; then
    echo "Installed. This Mac now sends its stats to $host, and will after every login."
    echo "Log: $LOG    Remove: bash $0 uninstall"
  else
    echo "Could not start the LaunchAgent; see: launchctl print gui/$(id -u)/$LABEL" >&2
    return 1
  fi
}

uninstall_agent() {
  launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null
  rm -f "$AGENT"
  rm -rf "$SUPPORT_DIR"
  echo "Removed. The SmallTV will show 'Waiting for the Mac' until you install it again."
}

usage() { sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; }

main() {
  case "${1:-}" in
    install)   [ -n "${2:-}" ] || { usage; exit 2; }; install_agent "$2" ;;
    uninstall) uninstall_agent ;;
    run)       [ -n "${2:-}" ] || { usage; exit 2; }; run_loop "$2" ;;
    print)
      echo "cpu  $(sample_cpu || echo '?') %"
      echo "mem  $(sample_mem || echo '?') %"
      echo "disk $(sample_disk || echo '?') %"
      echo "net  interface $(default_iface || echo '?'), bytes in/out: $(sample_net_bytes || echo '?')" ;;
    *) usage; exit 2 ;;
  esac
}

# Sourced by the tests with SMALLTV_STATS_LIB=1: define the functions only.
[ "${SMALLTV_STATS_LIB:-0}" = 1 ] || main "$@"
